#include "bmp_image.h"
#include "pack_defines.h"

#include <array>
#include <fstream>
#include <string_view>

using namespace std;

namespace img_lib {

// Структура заголовка BMP-файла (14 байт)
PACKED_STRUCT_BEGIN BitmapFileHeader {
    uint16_t bfType = 0x4D42;           // 2 байта: сигнатура 'BM' для BMP
    uint32_t bfSize = 0;                // 4 байта: полный размер файла в байтах
    uint32_t bfReserved = 0;            // 4 байта: зарезервировано, должно быть 0
    uint32_t bfOffBits = 54;            // 4 байта: смещение до начала пиксельных данных (14 + 40)
}
PACKED_STRUCT_END

// Структура информационного заголовка BMP (40 байт)
PACKED_STRUCT_BEGIN BitmapInfoHeader {
    uint32_t biSize = 40;               // Размер этой структуры (40 байт)
    int32_t  biWidth = 0;               // Ширина изображения в пикселях
    int32_t  biHeight = 0;              // Высота изображения (если > 0 — снизу вверх)
    uint16_t biPlanes = 1;              // Количество цветовых плоскостей (всегда 1)
    uint16_t biBitCount = 24;           // Количество бит на пиксель (24 бита: по 8 на RGB)
    uint32_t biCompression = 0;         // Тип сжатия (0 = без сжатия)
    uint32_t biSizeImage = 0;           // Размер изображения в байтах (включая отступы строк)
    int32_t  biXPelsPerMeter = 11811;   // Горизонтальное разрешение (в пикселях на метр, ~300 DPI)
    int32_t  biYPelsPerMeter = 11811;   // Вертикальное разрешение
    uint32_t biClrUsed = 0;             // Количество используемых цветов палитры (0 — все)
    uint32_t biClrImportant = 0x1000000;// Количество важных цветов (по умолчанию 16777216)
}
PACKED_STRUCT_END

// Вычисление длины строки в байтах с учетом выравнивания до 4 байт (BMP-требование)
static int GetBMPStride(int w) { // w - ширина изображения
    static const int size_of_pixel = 3; // размер пикселя в байтах
    static const int padding = 4; // величина выравнивания 
    return padding * ((w * size_of_pixel + 3) / 4); // каждый пиксель 3 байта (BGR), +3 округляет вверх до кратного 4
}

// Сохраняет изображение в формате BMP (24 бита, без сжатия)
bool SaveBMP(const Path& file, const Image& image) {
    const int width = image.GetWidth();
    const int height = image.GetHeight();
    const int stride = GetBMPStride(width); // ширина строки в байтах с выравниванием
    const uint32_t image_size = stride * height;

    BitmapFileHeader file_header;
    BitmapInfoHeader info_header;

    file_header.bfSize = sizeof(file_header) + sizeof(info_header) + image_size; // общий размер файла
    info_header.biWidth = width;
    info_header.biHeight = height;
    info_header.biSizeImage = image_size;

    ofstream out(file, ios::binary); // открываем файл для записи в бинарном режиме

    out.write(reinterpret_cast<const char*>(&file_header), sizeof(file_header)); // запись заголовка файла
    out.write(reinterpret_cast<const char*>(&info_header), sizeof(info_header)); // запись инфо-заголовка

    vector<uint8_t> row(stride, 0);  // временный буфер для строки с выравниванием

    for (int y = height - 1; y >= 0; --y) { // BMP хранит строки снизу вверх
        const auto* line = image.GetLine(y);
        for (int x = 0; x < width; ++x) {
            row[3 * x + 0] = static_cast<uint8_t>(line[x].b); // BMP: порядок BGR
            row[3 * x + 1] = static_cast<uint8_t>(line[x].g);
            row[3 * x + 2] = static_cast<uint8_t>(line[x].r);
        }
        out.write(reinterpret_cast<const char*>(row.data()), stride); // запись строки
    }

    return out.good();
}

// Загружает BMP-файл и возвращает изображение Image
Image LoadBMP(const Path& file) {
    ifstream in(file, ios::binary); // открываем файл в бинарном режиме
    if (!in) return {}; // не удалось открыть — возвращаем пустой объект

    BitmapFileHeader file_header;
    BitmapInfoHeader info_header;

    in.read(reinterpret_cast<char*>(&file_header), sizeof(file_header)); // читаем заголовки
    if (!in) {
        // Произошла ошибка при чтении заголовка
        return {};
    }

    in.read(reinterpret_cast<char*>(&info_header), sizeof(info_header));
    if (!in) {
        // Произошла ошибка при чтении заголовка
        return {};
    }

    // проверка соответствия формату BMP 24-bit
    if (file_header.bfType != 0x4D42 ||
        file_header.bfOffBits != 54 ||
        info_header.biSize != 40 ||
        info_header.biPlanes != 1 ||
        info_header.biBitCount != 24 ||
        info_header.biCompression != 0 ||
        info_header.biWidth <= 0 || info_header.biHeight <= 0) {
        return {}; // неподдерживаемый формат
    }

    const int width = info_header.biWidth;
    const int height = info_header.biHeight;
    const int stride = GetBMPStride(width);

    Image image(stride / 3, height, Color::Black());
    vector<uint8_t> row(stride);

    in.seekg(file_header.bfOffBits, ios::beg); // переходим к началу пикселей

    for (int y = height - 1; y >= 0; --y) { // BMP: строки снизу вверх
        in.read(reinterpret_cast<char*>(row.data()), stride);
        if (in.gcount() != stride) return {}; // ошибка чтения

        auto* line = image.GetLine(y);
        for (int x = 0; x < width; ++x) {
            line[x].b = static_cast<std::byte>(row[3 * x + 0]); // порядок: B
            line[x].g = static_cast<std::byte>(row[3 * x + 1]); // порядок: G
            line[x].r = static_cast<std::byte>(row[3 * x + 2]); // порядок: R
        }
    }

    return image; // успех
}


}  // namespace img_lib