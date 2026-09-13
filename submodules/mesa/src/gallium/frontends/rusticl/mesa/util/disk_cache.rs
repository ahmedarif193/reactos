// Copyright 2022 Red Hat.
// SPDX-License-Identifier: MIT

use libc_rust_gen::free;
use mesa_rust_gen::*;

use std::ffi::{c_void, CStr};
use std::ops::Deref;
use std::ptr;
use std::ptr::NonNull;
use std::slice;

pub struct DiskCacheBorrowed {
    cache: NonNull<disk_cache>,
}

pub struct DiskCache {
    inner: DiskCacheBorrowed,
}

// disk_cache is thread safe
unsafe impl Sync for DiskCacheBorrowed {}

impl DiskCacheBorrowed {
    pub fn from_ptr(cache: *mut disk_cache) -> Option<Self> {
        NonNull::new(cache).map(|c| Self { cache: c })
    }

    pub fn put(&self, data: &[u8], key: &mut cache_key) {
        unsafe {
            disk_cache_put(
                self.cache.as_ptr(),
                key,
                data.as_ptr().cast(),
                data.len(),
                ptr::null_mut(),
            );
        }
    }

    pub fn get(&self, key: &mut cache_key) -> Option<DiskCacheEntry<'_>> {
        let mut size = 0;

        unsafe {
            let data = disk_cache_get(self.cache.as_ptr(), key, &mut size);
            if data.is_null() {
                None
            } else {
                Some(DiskCacheEntry {
                    data: slice::from_raw_parts_mut(data.cast(), size),
                })
            }
        }
    }

    pub fn gen_key(&self, data: &[u8]) -> cache_key {
        let mut key = cache_key::default();

        unsafe {
            disk_cache_compute_key(
                self.cache.as_ptr(),
                data.as_ptr().cast(),
                data.len(),
                &mut key,
            );
        }

        key
    }

    pub fn as_ptr(s: &Option<Self>) -> *mut disk_cache {
        if let Some(s) = s {
            s.cache.as_ptr()
        } else {
            ptr::null_mut()
        }
    }
}

impl DiskCache {
    pub fn new(name: &CStr, func_ptrs: &[*mut c_void], flags: u64) -> Option<Self> {
        let mut blake3_ctx = blake3_hasher::default();
        let mut blake3 = [0; BLAKE3_KEY_LEN as usize];
        let mut cache_id = [0; BLAKE3_HEX_LEN as usize];

        let cache = unsafe {
            _mesa_blake3_init(&mut blake3_ctx);

            for &func_ptr in func_ptrs {
                if !disk_cache_get_function_identifier(func_ptr, &mut blake3_ctx) {
                    return None;
                }
            }
            _mesa_blake3_final(&mut blake3_ctx, &mut blake3);
            mesa_bytes_to_hex(cache_id.as_mut_ptr(), blake3.as_ptr(), blake3.len() as u32);
            disk_cache_create(name.as_ptr(), cache_id.as_ptr(), flags)
        };

        DiskCacheBorrowed::from_ptr(cache).map(|c| Self { inner: c })
    }
}

impl Deref for DiskCache {
    type Target = DiskCacheBorrowed;

    fn deref(&self) -> &Self::Target {
        &self.inner
    }
}

impl Drop for DiskCache {
    fn drop(&mut self) {
        unsafe {
            disk_cache_destroy(self.cache.as_ptr());
        }
    }
}

pub struct DiskCacheEntry<'a> {
    data: &'a mut [u8],
}

impl Deref for DiskCacheEntry<'_> {
    type Target = [u8];

    fn deref(&self) -> &Self::Target {
        self.data
    }
}

impl Drop for DiskCacheEntry<'_> {
    fn drop(&mut self) {
        unsafe {
            free(self.data.as_mut_ptr().cast());
        }
    }
}
