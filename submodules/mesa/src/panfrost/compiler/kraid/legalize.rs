// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::builder::*;
use crate::ir::*;
use crate::model::FAUModel;
use compiler::bitset::BitSet;
use compiler::enum_as_u8::EnumAsU8;
use kraid_proc_macros::EnumAsU8;

fn move_src_to_tmp(b: &mut impl SSABuilder, src: &mut Src) {
    // SrcRef::bytes() isn't totally accurate for zero but that's okay since
    // we should never copy it anyway.
    assert!(!matches!(&src.src_ref, SrcRef::Zero));

    let bytes = src.src_ref.bytes_read();
    debug_assert!(bytes <= 8);
    let tmp = b.alloc_ref((bytes * 8).into());
    let src_ref = std::mem::replace(&mut src.src_ref, tmp.clone().into());
    b.copy_to(tmp.into(), DataType::i(bytes * 8), src_ref.into());
}

fn legalize_imm_src(b: &mut impl SSABuilder, op: &mut Op, src_idx: usize) {
    let src = &op.srcs()[src_idx];
    let SrcRef::Imm32(imm32) = &src.src_ref else {
        return;
    };
    if !b.model().op_src_supports_imm32(op, src, (*imm32).into()) {
        move_src_to_tmp(b, &mut op.srcs_mut()[src_idx]);
    }
}

#[derive(Default)]
struct SSAValueSet {
    set: BitSet<SSAValue>,
    vec: Vec<SSAValue>,
}

impl SSAValueSet {
    fn new() -> SSAValueSet {
        Default::default()
    }

    fn clear(&mut self) {
        for ssa in self.vec.drain(..) {
            self.set.remove(ssa);
        }
    }

    fn is_empty(&self) -> bool {
        self.vec.is_empty()
    }

    fn insert(&mut self, ssa: SSAValue) -> bool {
        if self.set.insert(ssa) {
            self.vec.push(ssa);
            true
        } else {
            false
        }
    }
}

/// Legalizes vector sources by ensuring the following
///
///  1. For any vector source (SSARef::comps() > 1), all the SSAValues
///     referenced by the SSARef are unique.
///
///  2. For any two vector sources, either they are identical or they have
///     no SSAValues in common.
fn legalize_vec_srcs(
    b: &mut impl SSABuilder,
    instr: &mut Instr,
    ssa_used: &mut SSAValueSet,
) {
    debug_assert!(ssa_used.is_empty());
    let srcs = instr.srcs_mut();

    let mut duplicates = [!0_usize; 4];
    debug_assert!(srcs.len() <= duplicates.len());
    for i in 0..srcs.len() {
        if !srcs[i].src_ref.as_ssa().is_some_and(|ssa| ssa.comps() > 1) {
            continue;
        }

        for j in 0..i {
            if srcs[i].src_ref == srcs[j].src_ref {
                duplicates[i] = j;
                break;
            }
        }
        if duplicates[i] != !0_usize {
            continue;
        }

        let vec = srcs[i].src_ref.as_mut_ssa().unwrap();
        for ssa in vec {
            if !ssa_used.insert(*ssa) {
                *ssa = b.copy_ssa(*ssa);
            }
        }
    }

    for i in 0..srcs.len() {
        if duplicates[i] != !0_usize {
            srcs[i].src_ref = srcs[duplicates[i]].src_ref.clone();
        }
    }

    ssa_used.clear();
}

#[repr(u8)]
#[derive(Clone, Copy, EnumAsU8, PartialEq)]
enum HWFAUPage {
    User0,
    User1,
    User2,
    User3,
    Special0,
    Special1,
    Special3,
    // This one has to go last
    SmallConst,
}

fn hw_fau_page(fau_model: &FAUModel, fau: &FAURef) -> HWFAUPage {
    match fau.page {
        FAUPage::User => match fau_model.user_page_idx(fau.idx) {
            0 => HWFAUPage::User0,
            1 => HWFAUPage::User1,
            2 => HWFAUPage::User2,
            3 => HWFAUPage::User3,
            _ => panic!("Invalid user FAU page"),
        },
        FAUPage::Special0 => HWFAUPage::Special0,
        FAUPage::Special1 => HWFAUPage::Special1,
        FAUPage::Special3 => HWFAUPage::Special3,
        FAUPage::SmallConst => HWFAUPage::SmallConst,
    }
}

/// Legalizes FAU sources by ensuring the following
///
///  1. The combined amount of FAU (including small constants) is at most
///     64 bits, not including k0.
///
///  2. All FAUs come from the same page
///
///  3. Message instructions are not allowed to read from LaneId or anything
///     in FAUPage::Special3.
///
fn legalize_fau_srcs(
    b: &mut impl SSABuilder,
    fau_model: &FAUModel,
    op: &mut Op,
) {
    // Deal with the message special cases first
    if b.model().op_is_message(op) {
        for src in op.srcs_mut() {
            if let SrcRef::FAU(fau) = &src.src_ref {
                let is_warp_id =
                    fau.page == FAUPage::Special0 && fau.idx == 0b0010;
                if fau.page == FAUPage::Special3 || is_warp_id {
                    move_src_to_tmp(b, src);
                }
            }
        }
    }

    const _: () = {
        // SmallConst has to come last
        assert!(HWFAUPage::MAX_DISCRIMINANT == (HWFAUPage::SmallConst as u8));
    };

    let mut total_words_read = 0_u8;
    let mut pages_read = <HWFAUPage as EnumAsU8>::VariantSet::new();
    let mut page_words_read = [0_u8; HWFAUPage::SmallConst as u8 as usize];
    for src in op.srcs() {
        if let SrcRef::FAU(fau) = &src.src_ref {
            let page = hw_fau_page(fau_model, fau);
            let words = 1 + (fau.load64 as u8);
            total_words_read += words;
            if page != HWFAUPage::SmallConst {
                pages_read.insert(page);
                page_words_read[usize::from(page.as_u8())] += words;
            }
        }
    }

    if total_words_read <= 2 && pages_read.len() == 1 {
        return;
    }

    // Select the page with the most words used
    let mut page = 0;
    for i in 1..usize::from(HWFAUPage::SmallConst.as_u8()) {
        if page_words_read[i] > page_words_read[page] {
            page = i;
        }
    }
    let page = page as u8;

    // Figure out what sources to keep
    let mut fau_srcs = 0_u8;
    let mut fau_words_read = 0_u8;
    for (i, src) in op.srcs().iter().enumerate() {
        if let SrcRef::FAU(fau) = &src.src_ref {
            if hw_fau_page(fau_model, fau).as_u8() == page {
                // We prioritize 64-bit sources
                if fau.load64 {
                    fau_srcs = 1 << i;
                    fau_words_read = 2;
                    break;
                } else {
                    fau_srcs |= 1 << i;
                    fau_words_read += 1;
                    if fau_words_read == 2 {
                        break;
                    }
                }
            }
        }
    }
    debug_assert!(fau_words_read <= 2);

    if fau_words_read < 2 {
        for (i, src) in op.srcs().iter().enumerate() {
            if let SrcRef::FAU(fau) = &src.src_ref {
                if fau.page == FAUPage::SmallConst {
                    fau_srcs |= 1 << i;
                    fau_words_read += 1;
                    if fau_words_read == 2 {
                        break;
                    }
                }
            }
        }
    }
    debug_assert!(fau_words_read <= 2);

    for (i, src) in op.srcs_mut().iter_mut().enumerate() {
        if matches!(&src.src_ref, SrcRef::FAU(_)) {
            if ((fau_srcs >> i) & 1) == 0 {
                move_src_to_tmp(b, src);
            }
        }
    }
}

impl Shader<'_> {
    pub fn legalize(&mut self) {
        let model = self.model;
        let fau = model.fau();
        let mut ssa_used = SSAValueSet::new();
        self.map_instrs(|mut instr, ssa_alloc| {
            let mut b = SSAInstrBuilder::new(model, ssa_alloc);
            legalize_vec_srcs(&mut b, &mut instr, &mut ssa_used);
            for src_idx in 0..instr.srcs().len() {
                legalize_imm_src(&mut b, &mut instr, src_idx)
            }
            legalize_fau_srcs(&mut b, fau, &mut instr);
            b.push_instr(instr);
            b.into_mapped()
        });
    }
}
