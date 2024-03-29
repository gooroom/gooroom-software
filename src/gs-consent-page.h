/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2020 Gooroom <gooroom@gooroom.kr>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __GS_CONSET_PAGE_H
#define __GS_CONSET_PAGE_H

#include "gs-page.h" 

G_BEGIN_DECLS

#define GS_TYPE_CONSENT_PAGE (gs_consent_page_get_type ())

G_DECLARE_FINAL_TYPE (GsConsentPage, gs_consent_page, GS, CONSENT_PAGE, GsPage)

GsConsentPage    *gs_consent_page_new		(void);

G_END_DECLS

#endif /* __GS_CONSENT_PAGE_H*/
