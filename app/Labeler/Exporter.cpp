#include "pch.h"
#include "Exporter.h"

namespace imqs {
namespace anno {

static void WriteRawCifar10(const xo::Texture& tex, uint8_t label, uint8_t* buf) {
	// 1st byte is label
	buf[0] = label;
	buf++;

	// after label follows RGB, either planar, or packed

	bool planar = true;
	if (planar) {
		// RRR GGG BBB
		for (int chan = 0; chan < 3; chan++) {
			for (uint32_t y = 0; y < tex.Height; y++) {
				auto src = (uint8_t*) tex.DataAtLine(y);
				src += chan;
				auto dst    = buf + y * tex.Width;
				int  srcBPP = (int) tex.BytesPerPixel();
				for (uint32_t x = 0; x < tex.Width; x++) {
					*dst = *src;
					dst++;
					src += srcBPP;
				}
			}
			buf += tex.Width * tex.Height;
		}
	} else {
		// RGB RGB RGB
		for (uint32_t y = 0; y < tex.Height; y++) {
			auto src    = (uint8_t*) tex.DataAtLine(y);
			auto dst    = buf + y * tex.Width * 3;
			int  srcBPP = (int) tex.BytesPerPixel();
			for (uint32_t x = 0; x < tex.Width; x++) {
				dst[0] = src[0];
				dst[1] = src[1];
				dst[2] = src[2];
				dst += 3;
				src += srcBPP;
			}
		}
	}
}

Error SavePng(const xo::Texture& tex, std::string filename) {
	FILE* f = fopen(filename.c_str(), "wb");
	if (!f)
		return Error::Fmt("Error opening %v for writing", filename);

	// libpng doesn't accept RGBA for an RGB image, and we don't want to save an alpha channel, so
	// we need to transform our data from RGBA to RGB before giving it to libpng
	uint8_t*  rgb  = (uint8_t*) imqs_malloc_or_die(tex.Width * tex.Height * 3);
	uint8_t** rows = (uint8_t**) imqs_malloc_or_die(tex.Height * sizeof(void*));
	for (int i = 0; i < (int) tex.Height; i++) {
		auto rgba = (uint8_t*) tex.DataAtLine(i);
		auto line = rgb + i * tex.Width * 3;
		rows[i]   = line;
		int width = tex.Width;
		for (int j = 0; j < width; j++) {
			*line++ = rgba[0];
			*line++ = rgba[1];
			*line++ = rgba[2];
			rgba += 4;
		}
	}

	// there's no libpng error checking here, but there should be (expect out of disk space)

	auto pp = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
	auto ip = png_create_info_struct(pp);
	png_init_io(pp, f);
	png_set_IHDR(pp, ip, tex.Width, tex.Height, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
	png_set_compression_level(pp, 1);
	png_write_info(pp, ip);
	png_write_image(pp, rows);
	png_write_end(pp, nullptr);
	png_destroy_write_struct(&pp, &ip);

	free(rows);
	free(rgb);

	fclose(f);

	return Error();
}

Error ExportLabeledImagePatches_Frame(ExportTypes type, std::string dir, int64_t frameTime, const ImageLabels& labels, const ohash::map<std::string, int>& labelToIndex, const xo::Image& frameImg) {
	if (labels.Labels.size() == 0)
		return Error();

	os::File f;
	if (type == ExportTypes::Cifar10) {
		auto err = f.Create(tsf::fmt("%v/%v.bin", dir, frameTime));
		if (!err.OK())
			return err;
	}

	int      dim     = labels.Labels[0].Rect.Width(); // assume all rectangles are the same size, and that width == height
	size_t   bufSize = 1 + dim * dim * 3;
	uint8_t* buf     = (uint8_t*) imqs_malloc_or_die(bufSize);

	for (const auto& patch : labels.Labels) {
		IMQS_ASSERT(patch.Rect.Width() == dim && patch.Rect.Height() == dim);
		auto patchTex = frameImg.Window(patch.Rect.X1, patch.Rect.Y1, patch.Rect.Width(), patch.Rect.Height());
		if (type == ExportTypes::Cifar10) {
			uint8_t klass = labelToIndex.get(patch.Class);
			WriteRawCifar10(patchTex, klass, buf);
			auto err = f.Write(buf, bufSize);
			if (!err.OK())
				return err;
		} else if (type == ExportTypes::Png) {
			auto classDir = dir + "/" + strings::Replace(patch.Class, " ", "_");
			auto err      = os::MkDirAll(classDir);
			if (!err.OK())
				return err;
			auto filename = classDir + "/" + tsf::fmt("%09d-%04d-%04d-%04d-%04d.png", frameTime, patch.Rect.X1, patch.Rect.Y1, patch.Rect.Width(), patch.Rect.Height());
			err           = SavePng(patchTex, filename);
			if (!err.OK())
				return err;
		}
	}

	free(buf);

	return Error();
}

Error ExportLabeledImagePatches_Video(ExportTypes type, std::string videoFilename, const VideoLabels& labels, ProgressCallback prog) {
	auto dir = ImagePatchDir(videoFilename);
	auto err = os::MkDirAll(dir);
	if (!err.OK())
		return err;

	VideoFile video;
	err = video.OpenFile(videoFilename);
	if (!err.OK())
		return err;

	xo::Image img;
	if (!img.Alloc(xo::TexFormatRGBA8, video.Width(), video.Height()))
		return Error("Out of memory allocating image buffer");

	// Establish a mapping from label (a string) to an integer class.
	// The ML libraries just want an integer for the class, not a string.
	ohash::map<std::string, int> labelToIndex;
	for (const auto& frame : labels.Frames) {
		for (const auto& lab : frame.Labels)
			labelToIndex.insert(lab.Class, labelToIndex.size());
	}

	int64_t lastFrameTime = 0;
	int64_t micro         = 1000000;
	bool    abort         = false;

	for (size_t i = 0; i < labels.Frames.size() && !abort; i++) {
		const auto& frame = labels.Frames[i];
		// Only seek if frame is more than 5 seconds into the future. Haven't measured optimal metric to use here.
		if (frame.Time - lastFrameTime > 5 * micro) {
			int64_t buffer = 3 * micro; // seek 3 seconds behind frame target
			err            = video.SeekToMicrosecond(frame.Time - buffer);
			if (!err.OK())
				return err;
		}

		while (true) {
			err = video.DecodeFrameRGBA(img.Width, img.Height, img.Data, img.Stride);
			if (!err.OK())
				return err;
			int64_t pts   = video.LastFrameTimeMicrosecond();
			lastFrameTime = pts;
			if (pts == frame.Time) {
				// found our frame
				ExportLabeledImagePatches_Frame(type, dir, frame.Time, frame, labelToIndex, img);
				if (prog != nullptr) {
					if (!prog(i, labels.Frames.size())) {
						abort = true;
						break;
					}
				}
				break;
			} else if (pts > frame.Time) {
				tsf::print("Fail to find frame %v\n", frame.Time);
				break;
			}
		}
	}

	return Error();
}

} // namespace anno
} // namespace imqs