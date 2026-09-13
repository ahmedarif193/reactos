-- SPDX-License-Identifier: MIT

-- `r` is predefined in the environment and is the equivalent of rnn.init(<gpu>)

function dbg(fmt, ...)
	-- io.write(string.format(fmt, ...))
end

function append_stats(reg, xs, base, stats)
	if not reg.ENABLED then
		return
	end

	local s = xs.descriptor_stats[base]

	stats.img  = stats.img  | s.img
	stats.tex  = stats.tex  | s.tex
	stats.samp = stats.samp | s.samp
	stats.ubo  = stats.ubo  | s.ubo
	stats.has_img  = stats.has_img  | xs.has_img
	stats.has_tex  = stats.has_tex  | xs.has_tex
	stats.has_samp = stats.has_samp | xs.has_samp
	stats.has_ubo  = stats.has_ubo  | xs.has_ubo
end

function get_stats(pkt, base)
	local pm4 = r.adreno_pm4_type3_packets;
	local stats = {
		img = 0,
		tex = 0,
		samp = 0,
		ubo = 0,
		has_img = 0,
		has_tex = 0,
		has_samp = 0,
		has_ubo = 0,
	}

	if (pkt == pm4.CP_EXEC_CS_INDIRECT) or
	   (pkt == pm4.CP_EXEC_CS) then
		append_stats(r.SP_CS_CONFIG, r.shaderstat.cs, base, stats)
		return stats
	end

	if (pkt == pm4.CP_DRAW_AUTO) or
	   (pkt == pm4.CP_DRAW_INDIRECT_MULTI) or
	   (pkt == pm4.CP_DRAW_INDX_OFFSET) then
		append_stats(r.SP_VS_CONFIG, r.shaderstat.vs, base, stats)
		append_stats(r.SP_HS_CONFIG, r.shaderstat.hs, base, stats)
		append_stats(r.SP_DS_CONFIG, r.shaderstat.ds, base, stats)
		append_stats(r.SP_GS_CONFIG, r.shaderstat.gs, base, stats)
		append_stats(r.SP_PS_CONFIG, r.shaderstat.fs, base, stats)
		return stats
	end

	return nil
end

-- Should the given descriptor be shown decoded with the specified descriptor
-- type.  Some combinations can be ruled out, based on TYPE or FMT, for example.
-- If it is ambiguous, be conservative and show the specified decoding.
function show_descriptor(desc, type, pkt, base, idx)

	dbg("%d %d %s %s\n", base, idx, pkt, type)

	local stats = get_stats(pkt, base)
	if stats then
		dbg("got stats: %x %x %x %x\n", stats.img, stats.tex, stats.samp, stats.ubo)
		local mask = 0
		if type == r.desctype.DESC_SAMPLER then
			if not stats.has_samp then
				return false
			end
			mask = stats.samp
		elseif type == r.desctype.DESC_UBO then
			if not stats.has_ubo then
				return false
			end
			mask = stats.ubo
		elseif type == r.desctype.DESC_WEIGHT then
			if not stats.has_img then
				return false
			end
			mask = stats.img
		else
			if not stats.has_ubo then
				return false
			end
			mask = stats.tex
		end

		-- if mask has overflowed then we just have to assume everything
		-- is used.. lua doesn't really have unsigned numbers, so under-
		-- flow will be a negative value
		dbg("idx=%d, mask=%x & %x => %d\n", idx, mask, 1 << idx, mask & (1 << idx))
		if mask >= 0 then
			if (mask & (1 << idx)) == 0 then
				dbg("unused!\n")
				return false
			end
		end
	end

	if (type == r.desctype.DESC_SAMPLER) or (type == r.desctype.DESC_UBO) then
		-- Everything beyond only applies to TEX_MEMOBJ descriptors
		return true
	end

	if type == r.desctype.DESC_BUFFER then
		return r.a6xx_tex_type.A6XX_TEX_BUFFER == desc.TYPE
	end

	-- For any other descriptor type, TEX_BUFFER is invalid
	if r.a6xx_tex_type.A6XX_TEX_BUFFER == desc.TYPE then
		return false
	end

	local is_yuv = (desc.FMT == r.a6xx_format.FMT6_R8_G8B8_2PLANE_420_UNORM) or
		       (desc.FMT == r.a6xx_format.FMT6_R8_G8_B8_3PLANE_420_UNORM)

	if type == r.desctype.DESC_MULTI_PLANE then
		return is_yuv
	end

	-- For any other descriptor type, is_yuv is invalid
	if is_yuv then
		return false
	end

	-- we could be more precise with some info about the enabled
	-- shader stages, ie.
	--
	--   * if no img/img_bindless instructions, don't show DESC_WEIGHT
	--   * if no indirect sampler/texture indexing, we know which
	--     descriptors are samplers vs memobj
	--

	return true
end
