#pragma once

#ifdef __cplusplus
#include "miniaudio.h"
#include "miniaudio_pcm.h"

ma_result ma_decoder_init_path(const fs::path& pFilePath, const ma_decoder_config* pConfig, ma_decoder* pDecoder);

ma_result ma_redbook_decoder_init_path_by_ext(const fs::path& filePath, ma_decoder* pDecoder, VirtualWavEx* vw, bool& isLossy, bool& isPCM);

#if defined(MINIAUDIO_IMPLEMENTATION) || defined(MA_IMPLEMENTATION)

// Helper wrapper to simplify dealing with paths on Windows
ma_result ma_decoder_init_path(const fs::path& pFilePath, const ma_decoder_config* pConfig, ma_decoder* pDecoder)
{
#ifdef _WIN32
	return ma_decoder_init_file_w(pFilePath.c_str(), pConfig, pDecoder);
#else
	return ma_decoder_init_file(pFilePath.c_str(), pConfig, pDecoder);
#endif
}

typedef enum {
	DAF_WAV,
	DAF_FLAC,
	DAF_MP3,
	DAF_PCM
} DecoderAudioFormats;

// Helper wrapper to open as redbook (44100kHz stereo s16le) audio and use the file extension to determine the order to try decoders
ma_result ma_redbook_decoder_init_path_by_ext(const fs::path& filePath, ma_decoder* pDecoder, VirtualWavEx* vw, bool& isLossy, bool& isPCM)
{
	ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_s16, 2, 44100);	
	isLossy = false;
	isPCM = false;

	DecoderAudioFormats tryorder[4] = {DAF_WAV, DAF_FLAC, DAF_MP3, DAF_PCM};
	const auto& extension = filePath.extension().u8string();

	// determine which format to try first based on file extension
	if(extension.size() >= 4)
	{
		if(CompareICase(extension.c_str(), ".wav"))
		{
			// it's wave, default try order is good
		}
		else if(CompareICase(extension.c_str(), ".flac"))
		{
			tryorder[0] = DAF_FLAC;
			tryorder[1] = DAF_WAV;
		}
		else if(CompareICase(extension.c_str(), ".mp3"))
		{
			tryorder[0] = DAF_MP3;
			tryorder[1] = DAF_WAV;
			tryorder[2] = DAF_FLAC;
		}
		else if(CompareICase(extension.c_str(), ".pcm") || CompareICase(extension.c_str(), ".raw"))
		{
			tryorder[0] = DAF_PCM;
			tryorder[1] = DAF_WAV;
			tryorder[2] = DAF_FLAC;
			tryorder[3] = DAF_MP3;
		}
	}

	const int num_tries = std::size(tryorder);
	int i;
	for(i = 0; i < num_tries; i++)
	{
		if(tryorder[i] == DAF_WAV)
		{
			decoderConfig.encodingFormat = ma_encoding_format_wav;
			if(MA_SUCCESS == ma_decoder_init_path(filePath, &decoderConfig, pDecoder)) break;				
		}
		else if(tryorder[i] == DAF_FLAC)
		{
			decoderConfig.encodingFormat = ma_encoding_format_flac;
			if(MA_SUCCESS == ma_decoder_init_path(filePath, &decoderConfig, pDecoder)) break;
		}
		else if(tryorder[i] == DAF_MP3)
		{
			decoderConfig.encodingFormat = ma_encoding_format_mp3;
			if(MA_SUCCESS == ma_decoder_init_path(filePath, &decoderConfig, pDecoder))
			{
				isLossy = true;
				break;
			}
		}
		else if(tryorder[i] == DAF_PCM)
		{
			if(MA_SUCCESS == ma_decoder_init_path_pcm(filePath, &decoderConfig, pDecoder, vw))
			{
				isPCM = true;
				break;
			}
		}
	}
	if(i == num_tries)
	{
		// no more formats to try, return false
		printf("  ERROR: No valid format found\n");
		return MA_ERROR;
	}
	return MA_SUCCESS;
}

#endif

#endif