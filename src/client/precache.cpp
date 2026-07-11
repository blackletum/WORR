/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

//
// cl_precache.c
//

#include "client.h"

#include <cerrno>

#if 0
// Testing for CL_ParsePlayerSkin()
static void test_parse_player_skin(const char* string, bool parse_dogtag, const char* expect_name, const char* expect_model, const char* expect_skin, const char* expect_dogtag)
{
    char name[MAX_QPATH];
    char model[MAX_QPATH];
    char skin[MAX_QPATH];
    char dogtag[MAX_QPATH];

    CL_ParsePlayerSkin(name, model, skin, dogtag, parse_dogtag, string);
    Q_assert(strcmp(name, expect_name) == 0);
    Q_assert(strcmp(model, expect_model) == 0);
    Q_assert(strcmp(skin, expect_skin) == 0);
    Q_assert(strcmp(dogtag, expect_dogtag) == 0);
}

void test_CL_ParsePlayerSkin(void)
{
    test_parse_player_skin("unnamed\\male/grunt\\default", true, "unnamed", "male", "grunt", "default");
    test_parse_player_skin("unnamed\\male/grunt", false, "unnamed", "male", "grunt", "default");
    test_parse_player_skin("unnamed\\male\\grunt", false, "unnamed", "male", "grunt", "default");

    test_parse_player_skin("unnamed\\/grunt\\default", true, "unnamed", "male", "grunt", "default");
    test_parse_player_skin("unnamed\\/grunt", false, "unnamed", "male", "grunt", "default");

    test_parse_player_skin("Name\\Model/Skin\\Dogtag", true, "Name", "Model", "Skin", "Dogtag");

    test_parse_player_skin("Name\\Model\\Dogtag", true, "Name", "male", "grunt", "Dogtag");
    test_parse_player_skin("Name\\\\Dogtag", true, "Name", "male", "grunt", "Dogtag");
    test_parse_player_skin("Name\\\\", true, "Name", "male", "grunt", "default");

    test_parse_player_skin("Name\\Model/Skin\\", true, "Name", "Model", "Skin", "default");
    test_parse_player_skin("Name\\male\\Dogtag", true, "Name", "male", "grunt", "Dogtag");
    test_parse_player_skin("Name\\female\\Dogtag", true, "Name", "female", "athena", "Dogtag");
}
#endif

/*
================
CL_ParsePlayerSkin

Breaks up playerskin into name (optional), model and skin components.
If model or skin are found to be invalid, replaces them with sane defaults.
================
*/
void CL_ParsePlayerSkin(char *name, char *model, char *skin, char *dogtag, bool parse_dogtag, const char *s)
{
    char buf[MAX_QPATH * 4];
    size_t len;
    char *t;

    len = strlen(s);
    Q_assert(len < sizeof(buf));
    Q_strlcpy(buf, s, sizeof(buf));

    // isolate the player's name
    size_t name_len;
    char *model_str;
    t = strchr(buf, '\\');
    if (t) {
        name_len = t - buf;
        *t = 0;
        model_str = t + 1;
    } else {
        model_str = buf;
        name_len = 0;
    }

    char *skin_str = NULL;
    // isolate the model name
    t = strchr(model_str, '/');
    if (!t && !parse_dogtag) {
        /* Using '\\' as a separator for the skin name is technically incorrect;
         * even early game code always produced "model/skin", yet the backslash
         * was considered as a separator.
         * This means it's probably a compatibility measure for even earlier code,
         * and probably not needed any more...
         * Still, keep it for compatibility's sake when dealing with userinfo
         * from non-rerelease servers. */
        t = strchr(model, '\\');
    }
    if (t) {
        skin_str = t + 1;
        *t = 0;
    }

    // isolate the dogtag name
    const char *dogtag_str = "";
    if (parse_dogtag) {
        char *search_str = skin_str ? skin_str : model_str;
        t = strchr(search_str, '\\');
        if (t) {
            dogtag_str = t + 1;
            *t = 0;
        }
    }

    // copy the player's name
    if (name) {
        Q_strnlcpy(name, buf, name_len, MAX_QPATH);
    }
    // fix empty model to male
    if (!*model_str)
        Q_strlcpy(model, "male", MAX_QPATH);
    else
        Q_strlcpy(model, model_str, MAX_QPATH);
    if (skin_str)
        Q_strlcpy(skin, skin_str, MAX_QPATH);
    else
        skin[0] = 0;
    if (!*dogtag_str)
        Q_strlcpy(dogtag, "default", MAX_QPATH);
    else
        Q_strlcpy(dogtag, dogtag_str, MAX_QPATH);

    // apply restrictions on skins
    if (cl_noskins->integer == 2 || !COM_IsPath(skin))
        goto default_skin;

    if (cl_noskins->integer || !COM_IsPath(model))
        goto default_model;

    return;

default_skin:
    if (!Q_stricmp(model, "female")) {
        Q_strlcpy(model, "female", MAX_QPATH);
        Q_strlcpy(skin, "athena", MAX_QPATH);
    } else {
default_model:
        Q_strlcpy(model, "male", MAX_QPATH);
        Q_strlcpy(skin, "grunt", MAX_QPATH);
    }
}

/*
================
CL_LoadClientinfo

================
*/
void CL_LoadClientinfo(clientinfo_t *ci, const char *s)
{
    int         i;
    char        model_name[MAX_QPATH];
    char        skin_name[MAX_QPATH];
    char        dogtag_name[MAX_QPATH];
    char        model_filename[MAX_QPATH];
    char        skin_filename[MAX_QPATH];
    char        brightskin_filename[MAX_QPATH];
    char        weapon_filename[MAX_QPATH];
    char        icon_filename[MAX_QPATH];
    char        dogtag_filename[MAX_QPATH];
    bool        parse_dogtag = cls.serverProtocol == PROTOCOL_VERSION_RERELEASE;

    CL_ParsePlayerSkin(ci->name, model_name, skin_name, dogtag_name, parse_dogtag, s);

    // model file
    Q_concat(model_filename, sizeof(model_filename),
             "players/", model_name, "/tris.md2");
    ci->model = R_RegisterModel(model_filename);
    if (!ci->model && Q_stricmp(model_name, "male")) {
        Q_strlcpy(model_name, "male", sizeof(model_name));
        Q_strlcpy(model_filename, "players/male/tris.md2", sizeof(model_filename));
        ci->model = R_RegisterModel(model_filename);
    }

    // skin file
    Q_concat(skin_filename, sizeof(skin_filename),
             "players/", model_name, "/", skin_name, ".pcx");
    ci->skin = R_RegisterSkin(skin_filename);

    // if we don't have the skin and the model was female,
    // see if athena skin exists
    if (!ci->skin && !Q_stricmp(model_name, "female")) {
        Q_strlcpy(skin_name, "athena", sizeof(skin_name));
        Q_strlcpy(skin_filename, "players/female/athena.pcx", sizeof(skin_filename));
        ci->skin = R_RegisterSkin(skin_filename);
    }

    // if we don't have the skin and the model wasn't male,
    // see if the male has it (this is for CTF's skins)
    if (!ci->skin && Q_stricmp(model_name, "male")) {
        // change model to male
        Q_strlcpy(model_name, "male", sizeof(model_name));
        Q_strlcpy(model_filename, "players/male/tris.md2", sizeof(model_filename));
        ci->model = R_RegisterModel(model_filename);

        // see if the skin exists for the male model
        Q_concat(skin_filename, sizeof(skin_filename),
                 "players/male/", skin_name, ".pcx");
        ci->skin = R_RegisterSkin(skin_filename);
    }

    // if we still don't have a skin, it means that the male model
    // didn't have it, so default to grunt
    if (!ci->skin) {
        // see if the skin exists for the male model
        Q_strlcpy(skin_name, "grunt", sizeof(skin_name));
        Q_strlcpy(skin_filename, "players/male/grunt.pcx", sizeof(skin_filename));
        ci->skin = R_RegisterSkin(skin_filename);
    }

    // brightskin file (optional)
    Q_concat(brightskin_filename, sizeof(brightskin_filename),
             "players/", model_name, "/", skin_name, "_brtskn.png");
    ci->brightskin = R_RegisterImage(brightskin_filename, IT_SKIN,
                                     static_cast<imageflags_t>(IF_KEEP_EXTENSION | IF_OPTIONAL));

    // weapon file
    for (i = 0; i < cl.numWeaponModels; i++) {
        Q_concat(weapon_filename, sizeof(weapon_filename),
                 "players/", model_name, "/", cl.weaponModels[i]);
        ci->weaponmodel[i] = R_RegisterModel(weapon_filename);
        if (!ci->weaponmodel[i] && !Q_stricmp(model_name, "cyborg")) {
            // try male
            Q_concat(weapon_filename, sizeof(weapon_filename),
                     "players/male/", cl.weaponModels[i]);
            ci->weaponmodel[i] = R_RegisterModel(weapon_filename);
        }
    }

    // icon file
    Q_concat(icon_filename, sizeof(icon_filename),
             "/players/", model_name, "/", skin_name, "_i.pcx");
    Q_strlcpy(ci->icon_name, icon_filename, sizeof(ci->icon_name));

    Q_strlcpy(ci->model_name, model_name, sizeof(ci->model_name));
    Q_strlcpy(ci->skin_name, skin_name, sizeof(ci->skin_name));
    Q_concat(dogtag_filename, sizeof(dogtag_filename), dogtag_name, ".pcx");
    Q_strlcpy(ci->dogtag_name, dogtag_filename, sizeof(ci->dogtag_name));

    // base info should be at least partially valid
    if (ci == &cl.baseclientinfo)
        return;

    // must have loaded all data types to be valid
    if (!ci->skin || !ci->model || !ci->weaponmodel[0]) {
        ci->skin = 0;
        ci->brightskin = 0;
        ci->icon_name[0] = 0;
        ci->model = 0;
        ci->weaponmodel[0] = 0;
        ci->model_name[0] = 0;
        ci->skin_name[0] = 0;
        ci->dogtag_name[0] = 0;
    }
}

/*
=================
CL_RegisterSounds
=================
*/
void CL_RegisterSounds(void)
{
    int i;
    char    *s;

    S_BeginRegistration();
    CL_RegisterTEntSounds();
    cl.sfx_hit_marker = S_RegisterSound("feedback/hit.ogg");
    for (i = 1; i < cl.csr.max_sounds; i++) {
        s = cl.configstrings[cl.csr.sounds + i];
        if (!s[0])
            break;
        cl.sound_precache[i] = S_RegisterSound(s);
    }
    S_EndRegistration();
}

/*
=================
CL_RegisterBspModels

Registers main BSP file and inline models
=================
*/
void CL_RegisterBspModels(void)
{
    char *name = cl.configstrings[cl.csr.models + 1];
    int i, ret;

    if (!name[0]) {
        Com_Error(ERR_DROP, "%s: no map set", __func__);
    }
    ret = BSP_Load(name, &cl.bsp);
    if (cl.bsp == NULL) {
        Com_Error(ERR_DROP, "Couldn't load %s: %s", name, BSP_ErrorString(ret));
    }

    if (cl.bsp->checksum != Q_atoi(cl.configstrings[cl.csr.mapchecksum])) {
        if (cls.demo.playback) {
            Com_WPrintf("Local map version differs from demo: %i != %s\n",
                        cl.bsp->checksum, cl.configstrings[cl.csr.mapchecksum]);
        } else {
            Com_Error(ERR_DROP, "Local map version differs from server: %i != %s",
                      cl.bsp->checksum, cl.configstrings[cl.csr.mapchecksum]);
        }
    }

    for (i = 2; i < cl.csr.max_models; i++) {
        name = cl.configstrings[cl.csr.models + i];
        if (!name[0] && i != MODELINDEX_PLAYER) {
            break;
        }
        if (name[0] == '*')
            cl.model_clip[i] = BSP_InlineModel(cl.bsp, name);
        else
            cl.model_clip[i] = NULL;
    }
}

/*
=================
CL_RegisterVWepModels

Builds a list of visual weapon models
=================
*/
void CL_RegisterVWepModels(void)
{
    int         i;
    char        *name;

    cl.numWeaponModels = 1;
    Q_strlcpy(cl.weaponModels[0], "weapon.md2", sizeof(cl.weaponModels[0]));

    // only default model when vwep is off
    if (!cl_vwep->integer) {
        return;
    }

    for (i = 2; i < cl.csr.max_models; i++) {
        name = cl.configstrings[cl.csr.models + i];
        if (!name[0] && i != MODELINDEX_PLAYER) {
            break;
        }
        if (name[0] != '#') {
            continue;
        }

        // special player weapon model
        Q_strlcpy(cl.weaponModels[cl.numWeaponModels++], name + 1, sizeof(cl.weaponModels[0]));

        if (cl.numWeaponModels == MAX_CLIENTWEAPONMODELS) {
            break;
        }
    }
}

/*
=================
CL_SetSky

=================
*/
void CL_SetSky(void)
{
    float       rotate = 0;
    int         autorotate = 1;
    vec3_t      axis;

    if (cl.csr.extended)
#if defined(_WIN32)
        sscanf_s(cl.configstrings[CS_SKYROTATE], "%f %d", &rotate, &autorotate);
#else
        sscanf(cl.configstrings[CS_SKYROTATE], "%f %d", &rotate, &autorotate);
#endif
    else
        rotate = Q_atof(cl.configstrings[CS_SKYROTATE]);

#if defined(_WIN32)
    if (sscanf_s(cl.configstrings[CS_SKYAXIS], "%f %f %f",
                 &axis[0], &axis[1], &axis[2]) != 3) {
#else
    if (sscanf(cl.configstrings[CS_SKYAXIS], "%f %f %f",
               &axis[0], &axis[1], &axis[2]) != 3) {
#endif
        Com_DPrintf("Couldn't parse CS_SKYAXIS\n");
        VectorClear(axis);
    }

    R_SetSky(cl.configstrings[CS_SKY], rotate, autorotate, axis);
}

/*
=================
CL_RegisterImage

Hack to handle RF_CUSTOMSKIN for remaster
=================
*/
static qhandle_t CL_RegisterImage(const char *s)
{
    // if it's in a subdir and has an extension, it's either a sprite or a skin
    // allow /some/pic.pcx escape syntax
    if (cl.csr.extended && *s != '/' && *s != '\\' && *COM_FileExtension(s)) {
        if (!FS_pathcmpn(s, CONST_STR_LEN("sprites/psx_flare")))
            return R_RegisterImage(s, IT_SPRITE, IF_DEFAULT_FLARE);

        if (!FS_pathcmpn(s, CONST_STR_LEN("sprites/")))
            return R_RegisterSprite(s);

        if (strchr(s, '/'))
            return R_RegisterSkin(s);
    }

    return R_RegisterTempPic(s);
}

/*
=================
CS_LoadShadowLight

In hindsight, I should have overloaded entity_state_t for
these parameters, but the shadow lights were done very
early on in KexQ2's development and I have to live with it.
=================
*/
static void CS_LoadShadowLight(int index, const char *s)
{
/*
		gi.configstring(CS_SHADOWLIGHTS + i, G_Fmt("{};{};{:1};{};{:1};{:1};{:1};{:1};{};{:1};{:1};{:1};{:1}",
			self->s.number,
			(int)shadowlightinfo[i].shadowlight.lighttype,
			shadowlightinfo[i].shadowlight.radius,
			shadowlightinfo[i].shadowlight.resolution,
			shadowlightinfo[i].shadowlight.intensity,
			shadowlightinfo[i].shadowlight.fade_start,
			shadowlightinfo[i].shadowlight.fade_end,
			shadowlightinfo[i].shadowlight.max_fade_dist,
			shadowlightinfo[i].shadowlight.lightstyle,
			shadowlightinfo[i].shadowlight.coneangle,
			shadowlightinfo[i].shadowlight.conedirection[0],
			shadowlightinfo[i].shadowlight.conedirection[1],
			shadowlightinfo[i].shadowlight.conedirection[2]).data());
*/
    int shadow_index = index - cl.csr.shadowlights;
    if (shadow_index < 0 || shadow_index >= (int)cl.csr.max_shadowlights ||
        shadow_index >= MAX_SHADOW_LIGHTS) {
        return;
    }

    auto *slot = &cl.shadowdefs[shadow_index];
    int parsed_number = 0;
    cl_shadow_light_t parsed_light = {};
    bool valid = false;

    // Parse every numeric field strictly before touching the live slot. This
    // prevents a malformed update from leaving a mixture of old and new
    // values, or retaining a previously valid light as stale state.
    do {
        enum { LEGACY_FIELD_COUNT = 12, FIELD_COUNT = 13 };
        char fields[FIELD_COUNT][CS_MAX_STRING_LENGTH];
        int field_count = 0;
        const char *field_start = s;

        if (!s || Q_strnlen(s, CS_MAX_STRING_LENGTH) >= CS_MAX_STRING_LENGTH)
            break;

        while (field_count < FIELD_COUNT) {
            const char *separator = strchr(field_start, ';');
            size_t length = separator ? (size_t)(separator - field_start)
                                      : strlen(field_start);
            if (!length || length >= sizeof(fields[0]))
                break;
            memcpy(fields[field_count], field_start, length);
            fields[field_count][length] = '\0';
            field_count++;
            if (!separator) {
                field_start = NULL;
                break;
            }
            field_start = separator + 1;
        }

        // A non-null cursor means there were too many fields (including a
        // trailing semicolon); exact field counts reject trailing garbage.
        if (field_start ||
            (field_count != LEGACY_FIELD_COUNT && field_count != FIELD_COUNT)) {
            break;
        }

        auto parse_int = [](const char *text, int *value) {
            char *end = NULL;
            errno = 0;
            long parsed = strtol(text, &end, 10);
            if (end == text)
                return false;
            while (end && isspace((unsigned char)*end))
                end++;
            if (errno == ERANGE || !end || *end ||
                parsed < INT_MIN || parsed > INT_MAX) {
                return false;
            }
            *value = (int)parsed;
            return true;
        };
        auto parse_float = [](const char *text, float *value) {
            char *end = NULL;
            errno = 0;
            float parsed = strtof(text, &end);
            if (end == text)
                return false;
            while (end && isspace((unsigned char)*end))
                end++;
            if (errno == ERANGE || !end || *end || !isfinite(parsed)) {
                return false;
            }
            *value = parsed;
            return true;
        };

        int light_type = 0;
        int field = 0;
        if (!parse_int(fields[field++], &parsed_number) ||
            !parse_int(fields[field++], &light_type) ||
            !parse_float(fields[field++], &parsed_light.radius) ||
            !parse_int(fields[field++], &parsed_light.resolution) ||
            !parse_float(fields[field++], &parsed_light.intensity) ||
            !parse_float(fields[field++], &parsed_light.fade_start) ||
            !parse_float(fields[field++], &parsed_light.fade_end)) {
            break;
        }
        if (field_count == FIELD_COUNT) {
            if (!parse_float(fields[field++], &parsed_light.max_fade_dist))
                break;
        }
        if (!parse_int(fields[field++], &parsed_light.lightstyle) ||
            !parse_float(fields[field++], &parsed_light.coneangle) ||
            !parse_float(fields[field++], &parsed_light.conedirection[0]) ||
            !parse_float(fields[field++], &parsed_light.conedirection[1]) ||
            !parse_float(fields[field++], &parsed_light.conedirection[2]) ||
            field != field_count) {
            break;
        }

        constexpr float MAX_SHADOWLIGHT_DISTANCE = 1048576.0f;
        constexpr float MAX_SHADOWLIGHT_INTENSITY = 65536.0f;
        constexpr int MAX_SHADOWLIGHT_RESOLUTION = 4096;
        if (parsed_number <= 0 || parsed_number >= (int)cl.csr.max_edicts ||
            (light_type != 0 && light_type != 1) ||
            parsed_light.radius <= 0.0f ||
            parsed_light.radius > MAX_SHADOWLIGHT_DISTANCE ||
            parsed_light.resolution < 0 ||
            parsed_light.resolution > MAX_SHADOWLIGHT_RESOLUTION ||
            parsed_light.intensity < 0.0f ||
            parsed_light.intensity > MAX_SHADOWLIGHT_INTENSITY ||
            parsed_light.fade_start < 0.0f ||
            parsed_light.fade_start > MAX_SHADOWLIGHT_DISTANCE ||
            parsed_light.fade_end < parsed_light.fade_start ||
            parsed_light.fade_end > MAX_SHADOWLIGHT_DISTANCE ||
            parsed_light.max_fade_dist < 0.0f ||
            parsed_light.max_fade_dist > MAX_SHADOWLIGHT_DISTANCE ||
            parsed_light.lightstyle < -1 ||
            parsed_light.lightstyle >= MAX_LIGHTSTYLES ||
            parsed_light.coneangle < 0.0f || parsed_light.coneangle >= 90.0f ||
            fabsf(parsed_light.conedirection[0]) > 1.0f ||
            fabsf(parsed_light.conedirection[1]) > 1.0f ||
            fabsf(parsed_light.conedirection[2]) > 1.0f ||
            (light_type &&
             (parsed_light.coneangle < 1.0f ||
              VectorLengthSquared(parsed_light.conedirection) < 1e-6f))) {
            break;
        }

        if (!light_type)
            parsed_light.coneangle = 0.0f;
        parsed_light.owner_entity = parsed_number;
        parsed_light.source_index = shadow_index;
        parsed_light.strict_pvs = true;
        parsed_light.ignore_owner_casters = false;
        valid = true;
    } while (false);

    memset(slot, 0, sizeof(*slot));
    if (valid) {
        slot->number = parsed_number;
        slot->light = parsed_light;
    } else if (s && *s) {
        Com_LPrintf(PRINT_WARNING,
                    "Ignoring malformed shadow light configstring %d\n",
                    index);
    }
}

/*
=================
CL_PrepRenderer

Call before entering a new level, or after changing dlls
=================
*/
void CL_PrepRenderer(void)
{
    int         i;
    char        *name;

    if (!cls.ref_initialized)
        return;
    if (!cl.mapname[0])
        return;     // no map loaded

    // register models, pics, and skins
    R_BeginRegistration(cl.mapname);

    CL_LoadState(LOAD_MODELS);

    CL_RegisterTEntModels();

    for (i = 2; i < cl.csr.max_models; i++) {
        name = cl.configstrings[cl.csr.models + i];
        if (!name[0] && i != MODELINDEX_PLAYER) {
            break;
        }
        if (name[0] == '#') {
            continue;
        }
        cl.model_draw[i] = R_RegisterModel(name);
    }

    CL_LoadState(LOAD_IMAGES);
    for (i = 1; i < cl.csr.max_images; i++) {
        name = cl.configstrings[cl.csr.images + i];
        if (!name[0]) {
            break;
        }
        cl.image_precache[i] = CL_RegisterImage(name);
    }
    
    cgame->TouchPics();

    CL_LoadState(LOAD_CLIENTS);
    for (i = 0; i < MAX_CLIENTS; i++) {
        name = cl.configstrings[cl.csr.playerskins + i];
        if (!name[0]) {
            continue;
        }
        CL_LoadClientinfo(&cl.clientinfo[i], name);
    }

    CL_LoadClientinfo(&cl.baseclientinfo, "unnamed\\male/grunt\\default");
    CL_RegisterForcedModels();

    // set sky textures and speed
    CL_SetSky();

    // load shadow lights
    if (cl.csr.shadowlights != (uint16_t)-1 && cl.csr.max_shadowlights > 0) {
        int n;
        for (n = cl.csr.shadowlights, i = 0;
             i < cl.csr.max_shadowlights && n < cl.csr.end; i++, n++) {
            if (*cl.configstrings[n]) {
                CS_LoadShadowLight(n, cl.configstrings[n]);
            }
        }
    }

    // the renderer can now free unneeded stuff
    R_EndRegistration();

    // clear any lines of console text
    Con_ClearNotify_f();

    SCR_UpdateScreen();

    // start the cd track
    OGG_Play();
}

static void CL_ParseItemColorConfigstring(int item_id, const char *s)
{
    if (item_id < 0 || item_id >= MAX_ITEMS)
        return;

    const int old_model = cl.item_color_model_index[item_id];
    if (old_model > 0 && old_model < cl.csr.max_models)
        cl.item_color_by_model[old_model].u32 = 0;

    cl.item_color_model_index[item_id] = 0;

    if (!s || !*s)
        return;

    int model_index = 0;
    int r = 0;
    int g = 0;
    int b = 0;
    if (sscanf(s, "%d %d %d %d", &model_index, &r, &g, &b) != 4)
        return;

    if (model_index <= 0 || model_index >= cl.csr.max_models)
        return;

    r = Q_clip(r, 0, 255);
    g = Q_clip(g, 0, 255);
    b = Q_clip(b, 0, 255);

    cl.item_color_by_model[model_index] = COLOR_RGB(r, g, b);
    cl.item_color_model_index[item_id] = model_index;
}

// parse configstring, internal method
static void update_configstring(int index)
{
    const char *s = cl.configstrings[index];

    if (index == cl.csr.maxclients) {
        cl.maxclients = Q_atoi(s);
        return;
    }

    if (index == cl.csr.airaccel) {
        cl.pmp.airaccelerate = cl.pmp.qwmode || Q_atoi(s);
        return;
    }

    if (index == cl.csr.models + 1) {
        if (!Com_ParseMapName(cl.mapname, s, sizeof(cl.mapname)))
            Com_Error(ERR_DROP, "%s: bad world model: %s", __func__, s);
        return;
    }

    if (index >= cl.csr.lights && index < cl.csr.lights + MAX_LIGHTSTYLES) {
        CL_SetLightStyle(index - cl.csr.lights, s);
        return;
    }

    if (cl.csr.itemcolors != (uint16_t)-1 &&
        index >= cl.csr.itemcolors && index < cl.csr.itemcolors + MAX_ITEMS) {
        CL_ParseItemColorConfigstring(index - cl.csr.itemcolors, s);
        return;
    }

    if (cl.csr.itemcolors == (uint16_t)-1 &&
        index >= CS_ITEM_COLORS && index < CS_ITEM_COLORS + MAX_ITEMS) {
        CL_ParseItemColorConfigstring(index - CS_ITEM_COLORS, s);
        return;
    }

    if (cls.state < ca_precached) {
        return;
    }

    if (index >= cl.csr.models + 2 && index < cl.csr.models + cl.csr.max_models) {
        int i = index - cl.csr.models;

        cl.model_draw[i] = R_RegisterModel(s);
        if (*s == '*')
            cl.model_clip[i] = BSP_InlineModel(cl.bsp, s);
        else
            cl.model_clip[i] = NULL;
        return;
    }

    if (index >= cl.csr.sounds && index < cl.csr.sounds + cl.csr.max_sounds) {
        cl.sound_precache[index - cl.csr.sounds] = S_RegisterSound(s);
        return;
    }

    if (index >= cl.csr.images && index < cl.csr.images + cl.csr.max_images) {
        cl.image_precache[index - cl.csr.images] = CL_RegisterImage(s);
        return;
    }

    if (index >= cl.csr.playerskins && index < cl.csr.playerskins + MAX_CLIENTS) {
        CL_LoadClientinfo(&cl.clientinfo[index - cl.csr.playerskins], s);
        return;
    }

    if (index == CS_CDTRACK) {
        OGG_Play();
        return;
    }

    if (index == CS_SKYROTATE || index == CS_SKYAXIS) {
        CL_SetSky();
        return;
    }

    if (cl.csr.shadowlights != (uint16_t)-1 &&
        index >= cl.csr.shadowlights &&
        index < cl.csr.shadowlights + cl.csr.max_shadowlights) {
        CS_LoadShadowLight(index, s);
        return;
    }
}

/*
=================
CL_UpdateConfigstring

A configstring update has been parsed.
=================
*/
void CL_UpdateConfigstring(int index)
{
    update_configstring(index);

    cgame->ParseConfigString(index, cl.configstrings[index]);
}
