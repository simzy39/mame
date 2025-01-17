// license:BSD-3-Clause
// copyright-holders:Nicola Salmoria
#include "emu.h"
#include "dogfgt.h"


/***************************************************************************

  Convert the color PROMs into a more useable format.

  Dog-Fight has both palette RAM and PROMs. The PROMs are used for tiles &
  pixmap, RAM for sprites.

***************************************************************************/

void dogfgt_state::dogfgt_palette(palette_device &palette) const
{
	uint8_t const *const color_prom = memregion("proms")->base();

	// first 16 colors are RAM
	for (int i = 0; i < 64; i++)
	{
		int bit0, bit1, bit2;

		// red component
		bit0 = BIT(color_prom[i], 0);
		bit1 = BIT(color_prom[i], 1);
		bit2 = BIT(color_prom[i], 2);
		int const r = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;

		// green component
		bit0 = BIT(color_prom[i], 3);
		bit1 = BIT(color_prom[i], 4);
		bit2 = BIT(color_prom[i], 5);
		int const g = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;

		// blue component
		bit0 = 0;
		bit1 = BIT(color_prom[i], 6);
		bit2 = BIT(color_prom[i], 7);
		int const b = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;

		palette.set_pen_color(i + 16, rgb_t(r, g, b));
	}
}


/***************************************************************************

  Callbacks for the TileMap code

***************************************************************************/

TILE_GET_INFO_MEMBER(dogfgt_state::get_tile_info)
{
	tileinfo.set(0,
			m_bgvideoram[tile_index],
			m_bgvideoram[tile_index + 0x400] & 0x03,
			0);
}


/***************************************************************************

  Start the video hardware emulation.

***************************************************************************/

void dogfgt_state::video_start()
{
	m_bg_tilemap = &machine().tilemap().create(*m_gfxdecode, tilemap_get_info_delegate(*this, FUNC(dogfgt_state::get_tile_info)), TILEMAP_SCAN_ROWS, 16, 16, 32, 32);

	m_bitmapram = std::make_unique<uint8_t[]>(BITMAPRAM_SIZE);
	save_pointer(NAME(m_bitmapram), BITMAPRAM_SIZE);

	m_screen->register_screen_bitmap(m_pixbitmap);
	save_item(NAME(m_pixbitmap));
}


/***************************************************************************

  Memory handlers

***************************************************************************/

void dogfgt_state::plane_select_w(uint8_t data)
{
	m_bm_plane = data;
}

uint8_t dogfgt_state::bitmapram_r(offs_t offset)
{
	if (m_bm_plane > 2)
	{
		popmessage("bitmapram_r offs %04x plane %d\n", offset, m_bm_plane);
		return 0;
	}

	return m_bitmapram[offset + BITMAPRAM_SIZE / 3 * m_bm_plane];
}

void dogfgt_state::internal_bitmapram_w(offs_t offset, uint8_t data)
{
	m_bitmapram[offset] = data;

	offset &= (BITMAPRAM_SIZE / 3 - 1);
	int x = 8 * (offset / 256);
	int y = offset % 256;

	for (int subx = 0; subx < 8; subx++)
	{
		int color = 0;

		for (int i = 0; i < 3; i++)
			color |= ((m_bitmapram[offset + BITMAPRAM_SIZE / 3 * i] >> subx) & 1) << i;

		if (flip_screen())
			m_pixbitmap.pix(y ^ 0xff, (x + subx) ^ 0xff) = PIXMAP_COLOR_BASE + 8 * m_pixcolor + color;
		else
			m_pixbitmap.pix(y, x + subx) = PIXMAP_COLOR_BASE + 8 * m_pixcolor + color;
	}
}

void dogfgt_state::bitmapram_w(offs_t offset, uint8_t data)
{
	if (m_bm_plane > 2)
	{
		popmessage("bitmapram_w offs %04x plane %d\n", offset, m_bm_plane);
		return;
	}

	internal_bitmapram_w(offset + BITMAPRAM_SIZE / 3 * m_bm_plane, data);
}

void dogfgt_state::bgvideoram_w(offs_t offset, uint8_t data)
{
	m_bgvideoram[offset] = data;
	m_bg_tilemap->mark_tile_dirty(offset & 0x3ff);
}

void dogfgt_state::scroll_w(offs_t offset, uint8_t data)
{
	m_scroll[offset] = data;
	m_bg_tilemap->set_scrollx(0, m_scroll[0] + 256 * m_scroll[1] + 256);
	m_bg_tilemap->set_scrolly(0, m_scroll[2] + 256 * m_scroll[3]);
}

void dogfgt_state::_1800_w(uint8_t data)
{
	/* bits 0 and 1 are probably text color (not verified because PROM is missing) */
	m_pixcolor = ((data & 0x01) << 1) | ((data & 0x02) >> 1);

	/* bits 4 and 5 are coin counters */
	machine().bookkeeping().coin_counter_w(0, data & 0x10);
	machine().bookkeeping().coin_counter_w(1, data & 0x20);

	/* bit 7 is screen flip */
	flip_screen_set(data & 0x80);

	/* other bits unused? */
	logerror("PC %04x: 1800 = %02x\n", m_maincpu->pc(), data);
}


/***************************************************************************

  Display refresh

***************************************************************************/

void dogfgt_state::draw_sprites( bitmap_ind16 &bitmap,const rectangle &cliprect )
{
	for (int offs = 0; offs < m_spriteram.bytes(); offs += 4)
	{
		if (m_spriteram[offs] & 0x01)
		{
			int sx = m_spriteram[offs + 3];
			int sy = (240 - m_spriteram[offs + 2]) & 0xff;
			int flipx = m_spriteram[offs] & 0x04;
			int flipy = m_spriteram[offs] & 0x02;
			if (flip_screen())
			{
				sx = 240 - sx;
				sy = 240 - sy;
				flipx = !flipx;
				flipy = !flipy;
			}

			m_gfxdecode->gfx(1)->transpen(bitmap,cliprect,
					m_spriteram[offs + 1] + ((m_spriteram[offs] & 0x30) << 4),
					(m_spriteram[offs] & 0x08) >> 3,
					flipx,flipy,
					sx,sy,0);
		}
	}
}


uint32_t dogfgt_state::screen_update(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect)
{
	if (m_lastflip != flip_screen() || m_lastpixcolor != m_pixcolor)
	{
		m_lastflip = flip_screen();
		m_lastpixcolor = m_pixcolor;

		for (int offs = 0; offs < BITMAPRAM_SIZE; offs++)
			internal_bitmapram_w(offs, m_bitmapram[offs]);
	}


	m_bg_tilemap->draw(screen, bitmap, cliprect, 0, 0);

	draw_sprites(bitmap, cliprect);

	copybitmap_trans(bitmap, m_pixbitmap, 0, 0, 0, 0, cliprect, PIXMAP_COLOR_BASE + 8 * m_pixcolor);
	return 0;
}
