#include "clean-source.hpp"
#include "common.hpp"
#include "obs/obs-integration.hpp"

namespace melton {

static const char *CfName(void *)
{
	return "Melton Clean Feed";
}

static void *CfCreate(obs_data_t *, obs_source_t *source)
{
	return source; // no per-instance state; the feed is the global clean renderer
}

static void CfDestroy(void *) {}

static uint32_t CfWidth(void *)
{
	RecordingManager *m = Integration::MaybeManager();
	uint32_t w = m ? m->Renderer().BaseWidth() : 0;
	if (!w) {
		obs_video_info ovi;
		w = obs_get_video_info(&ovi) ? ovi.base_width : 1920;
	}
	return w;
}

static uint32_t CfHeight(void *)
{
	RecordingManager *m = Integration::MaybeManager();
	uint32_t h = m ? m->Renderer().BaseHeight() : 0;
	if (!h) {
		obs_video_info ovi;
		h = obs_get_video_info(&ovi) ? ovi.base_height : 1080;
	}
	return h;
}

static void CfRender(void *, gs_effect_t *)
{
	RecordingManager *m = Integration::MaybeManager();
	if (!m || !m->Renderer().Running())
		return; // not armed/recording: transparent (arm it from the MELTON DUAL FEED dock)
	OBSSource out = m->Renderer().Output();
	if (out)
		obs_source_video_render(out);
}

static obs_properties_t *CfProperties(void *)
{
	obs_properties_t *p = obs_properties_create();
	obs_properties_add_text(p, "info",
				"Shows the clean feed. Turn on \"Arm clean feed\" in the MELTON DUAL FEED dock "
				"(or record) for it to show picture.",
				OBS_TEXT_INFO);
	return p;
}

void RegisterCleanFeedSource()
{
	obs_source_info info = {};
	info.id = "melton_clean_feed";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_DO_NOT_DUPLICATE;
	info.get_name = CfName;
	info.create = CfCreate;
	info.destroy = CfDestroy;
	info.get_width = CfWidth;
	info.get_height = CfHeight;
	info.video_render = CfRender;
	info.get_properties = CfProperties;
	obs_register_source(&info);
}

} // namespace melton
