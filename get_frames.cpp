#include <iostream>
#include <string>
#include <filesystem>
#include <cassert>
extern "C"{
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libavutil/imgutils.h>
    #include <libavutil/opt.h>
    #include <libswscale/swscale.h>
    #include <libswresample/swresample.h>
    #include <jpeglib.h>
}

void get_basename(std::string &base_name, std::string_view file_path, std::string_view dir_sep){
    auto pos = file_path.find_last_of(dir_sep);
    if (pos == std::string::npos){
        base_name = file_path;
    }
    else{
        base_name = file_path.substr(pos + 1);
    }
}

void get_filename(std::string &file_name){
    auto pos = file_name.find('.');
    if (pos != std::string::npos){
        file_name.erase(pos);
    }
}

int main(int argc, char *argv[]){
    std::string dir_sep = "/";
    #if defined(_WIN32)
        dir_sep = "\\";
    #endif
    std::string input = argv[1];
    std::string dir, basename;
    get_basename(basename, input, dir_sep);
    std::string dirname = basename;
    get_filename(dirname);
    dir = dirname + "_frames";
    int quality = 75;
    for (int i=0; i<argc; ++i){
        if (strcmp(argv[i], "-o") == 0 && argv[i+1]){
            dir = argv[i+1];
        }
        if (strcmp(argv[i], "-q") == 0 && argv[i+1]){
            quality = atoi(argv[i+1]);
            assert(quality >= 0 && quality <= 100);
        }
    }
    std::filesystem::create_directory(dir);
    AVFormatContext *inputFmtContxt = NULL;
    const AVCodec *decoder = NULL;
    AVCodecContext *decoderContxt = NULL;
    int ret = 0, video_stream_index = 0;
    ret = avformat_open_input(&inputFmtContxt, input.c_str(), NULL, NULL);
    if (ret < 0){
        std::cout << "Could not open input video" << std::endl;
    }
    ret = avformat_find_stream_info(inputFmtContxt, NULL);
    if (ret < 0){
        std::cout << "Could not find the stream info" << std::endl;
    }
    //prepare encoder and decoder
    for (int i=0; i<(int)inputFmtContxt->nb_streams; ++i){
        AVStream *in_stream = inputFmtContxt->streams[i];
        AVCodecParameters *in_par = in_stream->codecpar;
        if (in_par->codec_type == AVMEDIA_TYPE_VIDEO){
            video_stream_index = i;
            decoder = avcodec_find_decoder(in_par->codec_id);
            decoderContxt = avcodec_alloc_context3(decoder);
            avcodec_parameters_to_context(decoderContxt, in_par);
            decoderContxt->framerate = in_stream->r_frame_rate;
            decoderContxt->time_base = in_stream->time_base;
            avcodec_open2(decoderContxt, decoder, NULL);
        }
    }
    //prepare converters between yuv and bgr
    enum AVPixelFormat pix_fmt = AV_PIX_FMT_RGB24;
    int HEIGHT = decoderContxt->height;
    int WIDTH = decoderContxt->width;
    SwsContext *scaler = sws_getContext(WIDTH, HEIGHT, decoderContxt->pix_fmt, 
                                        WIDTH, HEIGHT, pix_fmt, SWS_BICUBIC, NULL, NULL, NULL);
    //SwsContext *bgr2yuv = sws_getContext(WIDTH, HEIGHT, bgr_pix_fmt,
    //                                     WIDTH, HEIGHT, encoderContxt->pix_fmt, SWS_BICUBIC, NULL, NULL, NULL);
    
    //prepare packet and frames
    int res = 0;
    AVPacket *packet = av_packet_alloc();
    // input(decoded) frame
    AVFrame *frame = av_frame_alloc();
    // frame of BGR 
    AVFrame *rgbframe = av_frame_alloc();
    rgbframe->width = decoderContxt->width;
    rgbframe->height = decoderContxt->height;
    rgbframe->format = pix_fmt;
    ret = av_frame_get_buffer(rgbframe, 0);
    uint8_t *buf = (uint8_t*) av_malloc(av_image_get_buffer_size(pix_fmt, decoderContxt->width, decoderContxt->height, 1));
    ret = av_image_fill_arrays(rgbframe->data, rgbframe->linesize, buf, pix_fmt, decoderContxt->width, decoderContxt->height, 1);
    //start decoding and capturing
    int count = 0;
    std::cout << "capturing" << std::endl;
    while (true){
        ret = av_read_frame(inputFmtContxt, packet);
        if (ret < 0){
            break;
        }
        AVStream *input_stream = inputFmtContxt->streams[packet->stream_index];
        if (input_stream->codecpar->codec_type == video_stream_index){
            res = avcodec_send_packet(decoderContxt, packet);
            while (res >= 0){
                res = avcodec_receive_frame(decoderContxt, frame);
                if (res == AVERROR(EAGAIN) || res == AVERROR_EOF){
                    break;
                }
                if (res >= 0){
                    sws_scale(scaler, frame->data, frame->linesize, 0, frame->height, rgbframe->data, rgbframe->linesize);
                    ++count;
                    std::string filename = dir + dir_sep + "frame_" + std::to_string(count) + ".jpeg";
                    struct jpeg_compress_struct cinfo;
                    struct jpeg_error_mgr jerr;
                    cinfo.err = jpeg_std_error(&jerr);
                    jpeg_create_compress(&cinfo);
                    FILE *f = fopen(filename.c_str(), "wb");
                    int stride = rgbframe->linesize[0];
                    jpeg_stdio_dest(&cinfo, f);
                    cinfo.image_width = rgbframe->width;
                    cinfo.image_height = rgbframe->height;
                    cinfo.input_components = 3;
                    cinfo.in_color_space = JCS_RGB;
                    jpeg_set_defaults(&cinfo);
                    jpeg_set_quality(&cinfo, quality, TRUE);
                    jpeg_start_compress(&cinfo, TRUE);
                    uint8_t *row = rgbframe->data[0];
                    for (int i=0; i<rgbframe->height; ++i){
                        jpeg_write_scanlines(&cinfo, &row, 1);
                        row += stride;
                    }
                    jpeg_finish_compress(&cinfo);
                    jpeg_destroy_compress(&cinfo);
                    fclose(f);
                }
            }
            av_frame_unref(frame);
        }
        av_packet_unref(packet);
    }
    //free memories
    av_packet_free(&packet);
    av_frame_free(&frame);
    av_frame_free(&rgbframe);
    avformat_free_context(inputFmtContxt);
    avcodec_free_context(&decoderContxt);
    av_freep(&buf);
    sws_freeContext(scaler);
    std::cout << "done" << std::endl;
    return 0;
}