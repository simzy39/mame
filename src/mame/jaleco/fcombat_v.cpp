// license:BSD-3-Clause
// copyright-holders:Tomasz Slanina
/***************************************************************************

    Jaleco fcombat

***************************************************************************/

#include "emu.h"
#include "fcombat.h"


TILE_GET_INFO_MEMBER(fcombat_state::get_bg_tile_info)
{
	int tileno, palno;  //32*16 x 32

	//palno = (tile_index - (tile_index / 32 * 16) * 32 * 16) / 32;

	tileno = m_bgdata_rom[tile_index];
	palno = 0x18; //m_user2_region[tile_index] >> 3;
	tileinfo.set(2, tileno, palno, 0);
}


/***************************************************************************

  Convert the color PROMs into a more useable format.

  The palette PROM is connected to the RGB output this way:

  bit 7 -- 220 ohm resistor  -- BLUE
        -- 470 ohm resistor  -- BLUE
        -- 220 ohm resistor  -- GREEN
        -- 470 ohm resistor  -- GREEN
        -- 1  kohm resistor  -- GREEN
        -- 220 ohm resistor  -- RED
        -- 470 ohm resistor  -- RED
  bit 0 -- 1  kohm resistor  -- RED

***************************************************************************/

void fcombat_state::fcombat_palette(palette_device &palette) const
{
	const u8 *color_prom = memregion("proms")->base();

	// create a lookup table for the palette
	for (int i = 0; i < 0x20; i++)
	{
		int bit0, bit1, bit2;

		// red component
		bit0 = BIT(color_prom[i], 0);
		bit1 = BIT(color_prom[i], 1);
		bit2 = BIT(color_prom[i], 2);
		const u8 r = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;

		// green component
		bit0 = BIT(color_prom[i], 3);
		bit1 = BIT(color_prom[i], 4);
		bit2 = BIT(color_prom[i], 5);
		const u8 g = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;

		// blue component
		bit0 = 0;
		bit1 = BIT(color_prom[i], 6);
		bit2 = BIT(color_prom[i], 7);
		const u8 b = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;

		palette.set_indirect_color(i, rgb_t(r, g, b));
	}

	// color_prom now points to the beginning of the lookup table
	color_prom += 0x20;

	// fg chars/sprites
	for (int i = 0; i < 0x200; i++)
	{
		const u8 ctabentry = (color_prom[(i & 0x1c0) | ((i & 3) << 4) | ((i >> 2) & 0x0f)] & 0x0f) | 0x10;
		palette.set_pen_indirect(i, ctabentry);
	}

	// bg chars (this is not the full story... there are four layers mixed using another PROM
	for (int i = 0x200; i < 0x300; i++)
	{
		const u8 ctabentry = color_prom[i] & 0x0f;
		palette.set_pen_indirect(i, ctabentry);
	}
}


/*************************************
 *
 *  Video system startup
 *
 *************************************/

void fcombat_state::video_start()
{
	m_bgmap = &machine().tilemap().create(*m_gfxdecode, tilemap_get_info_delegate(*this, FUNC(fcombat_state::get_bg_tile_info)), TILEMAP_SCAN_ROWS, 16, 16, 32 * 8 * 2, 32);
}


/*************************************
 *
 *  Video register I/O
 *
 *************************************/

void fcombat_state::videoreg_w(u8 data)
{
	/* bit 0 = flip screen and joystick input multiplexor */
	m_cocktail_flip = data & 1;

	/* bits 1-2 char lookup table bank */
	m_char_palette = (data & 0x06) >> 1;

	/* bits 3 char bank */
	m_char_bank = (data & 0x08) >> 3;

	/* bits 4-5 unused */

	/* bits 6-7 sprite lookup table bank */
	m_sprite_palette = 0;//(data & 0xc0) >> 6;
	//popmessage("%08x",data);
}


u32 fcombat_state::screen_update(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect)
{
	/* draw background */
	m_bgmap->set_scrolly(0, m_fcombat_sh);
	m_bgmap->set_scrollx(0, m_fcombat_sv - 24);

	m_bgmap->mark_all_dirty();
	m_bgmap->draw(screen, bitmap, cliprect, 0, 0);
	//draw_background(bitmap, cliprect);

	/* draw sprites */
	for (int i = 0; i < m_spriteram.bytes(); i += 4)
	{
		int flags = m_spriteram[i + 0];
		int y = m_spriteram[i + 1] ^ 255;
		int code = m_spriteram[i + 2] + ((flags & 0x20) << 3);
		int x = m_spriteram[i + 3] * 2 + 72;

		int xflip = flags & 0x80;
		int yflip = flags & 0x40;
		bool doubled = false;// flags & 0x10;
		const bool wide = flags & 0x08;
		int code2 = code;

		int color = ((flags >> 1) & 0x03) | ((code >> 5) & 0x04) | (code & 0x08) | (m_sprite_palette * 16);
		gfx_element *gfx = m_gfxdecode->gfx(1);

		if (m_cocktail_flip)
		{
			x = 64 * 8 - gfx->width() - x;
			y = 32 * 8 - gfx->height() - y;
			if (wide) y -= gfx->height();
			xflip = !xflip;
			yflip = !yflip;
		}

		if (wide)
		{
			if (yflip)
				code |= 0x10, code2 &= ~0x10;
			else
				code &= ~0x10, code2 |= 0x10;

			gfx->transpen(bitmap,cliprect, code2, color, xflip, yflip, x, y + gfx->height(), 0);
		}

		if (flags & 0x10)
		{
			gfx->transpen(bitmap,cliprect, code2 + 16, color, xflip, yflip, x, y + gfx->height(), 0);
			gfx->transpen(bitmap,cliprect, code2 + 16 * 2, color, xflip, yflip, x, y + 2 * gfx->height(), 0);
			gfx->transpen(bitmap,cliprect, code2 + 16 * 3, color, xflip, yflip, x, y + 3 * gfx->height(), 0);

		}

		gfx->transpen(bitmap,cliprect, code, color, xflip, yflip, x, y, 0);

		if (doubled) i += 4;
	}

	/* draw the visible text layer */
	for (int sy = VISIBLE_Y_MIN/8; sy < VISIBLE_Y_MAX/8; sy++)
		for (int sx = VISIBLE_X_MIN/8; sx < VISIBLE_X_MAX/8; sx++)
		{
			int x = m_cocktail_flip ? (63 * 8 - 8 * sx) : 8 * sx;
			int y = m_cocktail_flip ? (31 * 8 - 8 * sy) : 8 * sy;

			const int offs = sx + sy * 64;
			m_gfxdecode->gfx(0)->transpen(bitmap,cliprect,
				m_videoram[offs] + 256 * m_char_bank,
				((m_videoram[offs] & 0xf0) >> 4) + m_char_palette * 16,
				m_cocktail_flip, m_cocktail_flip, x, y, 0);
		}
	return 0;
}
