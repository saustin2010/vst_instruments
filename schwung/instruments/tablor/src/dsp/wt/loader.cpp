/* FLAC decode for .wtNNNN factory tables (FigBug's FLAC-compressed
 * wavetable format: a mono FLAC whose frame size is in the extension).
 * dr_flac by David Reid, public domain / MIT-0.
 */
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_OGG
#include "../../ported/dr_flac.h"

#include <vector>

namespace tb {

bool wtDecodeFlac(const char *path, std::vector<float> &samples, float &sampleRate)
{
    unsigned int channels = 0, rate = 0;
    drflac_uint64 frameCount = 0;
    float *data = drflac_open_file_and_read_pcm_frames_f32(
        path, &channels, &rate, &frameCount, nullptr);
    if (!data) return false;

    samples.resize((size_t) frameCount);
    for (drflac_uint64 i = 0; i < frameCount; i++)
        samples[(size_t) i] = data[i * channels];      /* channel 0 */

    drflac_free(data, nullptr);
    sampleRate = (float) rate;
    return frameCount > 0;
}

} // namespace tb
