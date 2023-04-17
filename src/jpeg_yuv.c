#include <stdio.h>
#include <jpeglib.h>
#include <stdlib.h>

// Function to decode a JPEG image and save it as YUV422 data
void decodeJPEG(const char* inputJpegFile, const char* outputYuvFile) {
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;

    FILE* infile = fopen(inputJpegFile, "rb");
    if (infile == NULL) {
        fprintf(stderr, "Can't open %s\n", inputJpegFile);
        return;
    }

    // Initialize the JPEG decompression object
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);

    // Specify the input file
    jpeg_stdio_src(&cinfo, infile);

    // Read header information
    jpeg_read_header(&cinfo, TRUE);

    // Start decompression
    jpeg_start_decompress(&cinfo);

    int* width=&cinfo.output_width;
    int* height=&cinfo.output_height;

    // Allocate memory for the RGB image
    unsigned char* rgbImage = (unsigned char*)malloc(3 * cinfo.output_width * cinfo.output_height);

    // Read scanlines and convert to RGB
    unsigned char* rowPtr = rgbImage;
    while (cinfo.output_scanline < cinfo.output_height) {
        jpeg_read_scanlines(&cinfo, &rowPtr, 1);
        rowPtr += 3 * cinfo.output_width;
    }

    // Finish decompression
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);

    fclose(infile);

    // Now you have the RGB image in 'rgbImage'

    // Convert RGB to YUV422
    int yuvSize = (*width) * (*height) * 2;  // YUV422 format
    unsigned char* yuvImage = (unsigned char*)malloc(yuvSize);
    printf("width:%d height:%d\n",*width,*height);

    for (int i = 0; i < *height; i++) {
        for (int j = 0; j < *width; j += 2) {
            int idx = (i * (*width) + j) * 2;
            int r1 = rgbImage[3 * idx];
            int g1 = rgbImage[3 * idx + 1];
            int b1 = rgbImage[3 * idx + 2];
            int r2 = rgbImage[3 * (idx + 1)];
            int g2 = rgbImage[3 * (idx + 1) + 1];
            int b2 = rgbImage[3 * (idx + 1) + 2];

            // Convert to YUV and store in YUV422 format
            yuvImage[idx] = (unsigned char)(0.299 * r1 + 0.587 * g1 + 0.114 * b1); // Y1
            // yuvImage[idx + 1] = (unsigned char)(0.564 * (b1 - yuvImage[idx]) + 128); // U
            yuvImage[idx + 1] = (unsigned char)(-0.169*r1-0.331*g1+0.499*b1+128); // U
            yuvImage[idx + 2] = (unsigned char)(0.299 * r2 + 0.587 * g2 + 0.114 * b2); // Y2
            yuvImage[idx + 3] = (unsigned char)(0.499*r2-0.418*g2-0.0813*b2+128); // v
            // yuvImage[idx + 3] = (unsigned char)(0.564 * (b2 - yuvImage[idx + 2]) + 128); // U
        }
    }

    // Save the YUV422 data to the YUV file
    FILE* yuvFile = fopen(outputYuvFile, "wb");
    if (yuvFile == NULL) {
        fprintf(stderr, "Can't open %s\n", outputYuvFile);
        free(rgbImage);
        free(yuvImage);
        return;
    }

    // Write the YUV422 data to the YUV file
    fprintf(yuvFile, "Width: %d\nHeight: %d\n", *width, *height);
    fwrite(yuvImage, 1, yuvSize, yuvFile);

    // Close the YUV output file
    fclose(yuvFile);

    // Don't forget to free yuvImage after it's no longer needed
    free(yuvImage);
    free(rgbImage);

}


void yuv_jpg(const char* yuvFile, const char* jpegFile) {
    FILE* yuvFilePtr = fopen(yuvFile, "rb");

    if (yuvFilePtr == NULL) {
        fprintf(stderr, "Can't open %s\n", yuvFile);
    }

    // 读取图像宽度和高度（假设以文本形式存储在 YUV 文件中）
    int width, height;
    if (fscanf(yuvFilePtr, "Width: %d\nHeight: %d\n", &width, &height) != 2) {
        fprintf(stderr, "Failed to read width and height from the YUV file.\n");
        fclose(yuvFilePtr);
    }

    printf("Image Width: %d\n", width);
    printf("Image Height: %d\n", height);

    // 计算 YUV 数据的大小，这里假设 YUV 格式为 YUV422
    int yuvSize = width * height * 2;

    // 分配内存来存储 YUV 数据
    unsigned char* yuvData = (unsigned char*)malloc(yuvSize);

    // 读取 YUV 数据
    fread(yuvData, 1, yuvSize, yuvFilePtr);

    // 初始化 libjpeg 的 JPEG 结构体
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);

    // 打开输出的 JPEG 文件
    FILE* jpegFilePtr = fopen(jpegFile, "wb");

    if (jpegFilePtr == NULL) {
        fprintf(stderr, "Can't open %s\n", jpegFile);
        fclose(yuvFilePtr);
        free(yuvData);
    }

    // 将输出文件流与 JPEG 结构体关联
    jpeg_stdio_dest(&cinfo, jpegFilePtr);

    // 设置 JPEG 编码参数
    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 3; // YUV422 转换为 RGB
    cinfo.in_color_space = JCS_YCbCr; // YUV 颜色空间

    jpeg_set_defaults(&cinfo);
    jpeg_start_compress(&cinfo, TRUE);

    JSAMPROW row_pointer[1];

    // 循环遍历 YUV 数据，将其转换为 RGB 并写入 JPEG 文件
    while (cinfo.next_scanline < cinfo.image_height) {
        row_pointer[0] = &yuvData[cinfo.next_scanline * width * 2];
        jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }

    // 完成 JPEG 编码
    jpeg_finish_compress(&cinfo);
    fclose(jpegFilePtr);

    // 清理 libjpeg 结构体
    jpeg_destroy_compress(&cinfo);

    // 关闭文件和释放内存
    fclose(yuvFilePtr);
    free(yuvData);
}


void jpg_rgb(const char* inputJpegFile, const char* outputRgbFile) {
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;

    FILE* infile = fopen(inputJpegFile, "rb");
    if (infile == NULL) {
        fprintf(stderr, "Can't open %s\n", inputJpegFile);
        return;
    }

    // Initialize the JPEG decompression object
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);

    // Specify the input file
    jpeg_stdio_src(&cinfo, infile);

    // Read header information
    jpeg_read_header(&cinfo, TRUE);

    // Start decompression
    jpeg_start_decompress(&cinfo);

    int* width=&cinfo.output_width;
    int* height=&cinfo.output_height;

    // Allocate memory for the RGB image
    unsigned char* rgbImage = (unsigned char*)malloc(3 * cinfo.output_width * cinfo.output_height);

    // Read scanlines and convert to RGB
    unsigned char* rowPtr = rgbImage;
    while (cinfo.output_scanline < cinfo.output_height) {
        jpeg_read_scanlines(&cinfo, &rowPtr, 1);
        rowPtr += 3 * cinfo.output_width;
    }

    // Finish decompression
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);

    fclose(infile);
    int rgbSize = (*width) * (*height) * 3;
    printf("width:%d height:%d\n",*width,*height);
    FILE* rgbFile = fopen(outputRgbFile, "wb");
    if (rgbFile == NULL) {
        fprintf(stderr, "Can't open %s\n", outputRgbFile);
        free(rgbImage);
        return;
    }
    fprintf(rgbFile, "Width: %d\nHeight: %d\n", *width, *height);
    fwrite(rgbImage, 1, rgbSize, rgbFile);
    fclose(rgbFile);
    free(rgbImage);
}



int main() {
    char inputJpegFile[256];  // Assuming a maximum file path length of 255 characters
    char outputYuvFile[256];
    char outputRgbFile[256];
    char outputJpegFile[256];

    printf("Enter the path to the input JPEG file: ");
    scanf("%s", inputJpegFile);
    printf("Enter the path to the output RGB file: ");
    scanf("%s", outputRgbFile);
    jpg_rgb(inputJpegFile,outputRgbFile);

    // printf("Enter the path to the output YUV file: ");
    // scanf("%s", outputYuvFile);

    // printf("Enter the path to the output JPEG file: ");
    // scanf("%s", outputJpegFile);

    // decodeJPEG(inputJpegFile, outputYuvFile);
    // yuv_jpg(outputYuvFile,outputJpegFile);

    return 0;
}
