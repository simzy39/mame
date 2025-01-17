// license:BSD-3-Clause
// copyright-holders:Bryan McPhail
#include "emu.h"
#include "bogeyman.h"


void bogeyman_state::bogeyman_palette(palette_device &palette) const
{
	uint8_t const *color_prom = memregion("proms")->base();

	// first 16 colors are RAM
	for (int i = 0; i < 256; i++)
	{
		int bit0, bit1, bit2;

		// red component
		bit0 = BIT(color_prom[0], 0);
		bit1 = BIT(color_prom[0], 1);
		bit2 = BIT(color_prom[0], 2);
		int const r = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;

		// green component
		bit0 = BIT(color_prom[0], 3);
		bit1 = BIT(color_prom[256], 0);
		bit2 = BIT(color_prom[256], 1);
		int const g = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;

		// blue component
		bit0 = 0;
		bit1 = BIT(color_prom[256], 2);
		bit2 = BIT(color_prom[256], 3);
		int const b = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;

		palette.set_pen_color(i + 16, rgb_t(r, g, b));
		color_prom++;
	}
}

void bogeyman_state::videoram_w(offs_t offset, uint8_t data)
{
	m_videoram[offset] = data;
	m_bg_tilemap->mark_tile_dirty(offset);
}

void bogeyman_state::colorram_w(offs_t offset, uint8_t data)
{
	m_colorram[offset] = data;
	m_bg_tilemap->mark_tile_dirty(offset);
}

void bogeyman_state::videoram2_w(offs_t offset, uint8_t data)
{
	m_videoram2[offset] = data;
	m_fg_tilemap->mark_tile_dirty(offset);
}

void bogeyman_state::colorram2_w(offs_t offset, uint8_t data)
{
	m_colorram2[offset] = data;
	m_fg_tilemap->mark_tile_dirty(offset);
}

TILE_GET_INFO_MEMBER(bogeyman_state::get_bg_tile_info)
{
	int attr = m_colorram[tile_index];
	int gfxbank = ((((attr & 0x01) << 8) + m_videoram[tile_index]) / 0x80) + 3;
	int code = m_videoram[tile_index] & 0x7f;
	int color = (attr >> 1) & 0x07;

	tileinfo.set(gfxbank, code, color, 0);
}

TILE_GET_INFO_MEMBER(bogeyman_state::get_fg_tile_info)
{
	int attr = m_colorram2[tile_index];
	int tile = m_videoram2[tile_index] | ((attr & 0x03) << 8);
	int gfxbank = tile / 0x200;
	int code = tile & 0x1ff;

	tileinfo.set(gfxbank, code, m_colbank, 0);
}

void bogeyman_state::video_start()
{
	m_bg_tilemap = &machine().tilemap().create(*m_gfxdecode, tilemap_get_info_delegate(*this, FUNC(bogeyman_state::get_bg_tile_info)), TILEMAP_SCAN_ROWS, 16, 16, 16, 16);
	m_fg_tilemap = &machine().tilemap().create(*m_gfxdecode, tilemap_get_info_delegate(*this, FUNC(bogeyman_state::get_fg_tile_info)), TILEMAP_SCAN_ROWS, 8, 8, 32, 32);

	m_fg_tilemap->set_transparent_pen(0);
}

void bogeyman_state::draw_sprites(bitmap_ind16 &bitmap, const rectangle &cliprect)
{
	for (int offs = 0; offs < m_spriteram.bytes(); offs += 4)
	{
		int attr = m_spriteram[offs];

		if (attr & 0x01)
		{
			int code = m_spriteram[offs + 1] + ((attr & 0x40) << 2);
			int color = (attr & 0x08) >> 3;
			int flipx = !(attr & 0x04);
			int flipy = attr & 0x02;
			int sx = m_spriteram[offs + 3];
			int sy = (240 - m_spriteram[offs + 2]) & 0xff;
			int multi = attr & 0x10;

			if (multi) sy -= 16;

			if (flip_screen())
			{
				sx = 240 - sx;
				sy = 240 - sy;
				flipx = !flipx;
				flipy = !flipy;
			}


				m_gfxdecode->gfx(2)->transpen(bitmap,cliprect,
				code, color,
				flipx, flipy,
				sx, sy, 0);

			if (multi)
			{
					m_gfxdecode->gfx(2)->transpen(bitmap,cliprect,
					code + 1, color,
					flipx, flipy,
					sx, sy + (flip_screen() ? -16 : 16), 0);
			}
		}
	}
}

uint32_t bogeyman_state::screen_update(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect)
{
	m_bg_tilemap->draw(screen, bitmap, cliprect, 0, 0);
	draw_sprites(bitmap, cliprect);
	m_fg_tilemap->draw(screen, bitmap, cliprect, 0, 0);
	return 0;
}
