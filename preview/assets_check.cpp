#include <png.h>
#include <cassert>
#include <filesystem>
#include <vector>

int main(int argc,char **argv) {
    assert(argc==2);
    const auto root=std::filesystem::path(argv[1]);
    assert(std::filesystem::file_size(root/"DejaVuSans.ttf")>0);
    for(const char *name : {"cross","circle","square","triangle","options","dpad","l1","r1"}) {
        png_image image{};image.version=PNG_IMAGE_VERSION;
        const auto path=root/"buttons"/(std::string(name)+".png");
        assert(std::filesystem::file_size(path)<=32768); // Native PS4 resource read limit.
        assert(png_image_begin_read_from_file(&image,path.c_str()));
        assert(image.width==128 && image.height==128 && (image.format&PNG_FORMAT_FLAG_ALPHA));
        image.format=PNG_FORMAT_RGBA;
        std::vector<unsigned char> pixels(PNG_IMAGE_SIZE(image));
        assert(png_image_finish_read(&image,nullptr,pixels.data(),0,nullptr));
        bool transparent=false,opaque=false;
        for(std::size_t i=3;i<pixels.size();i+=4) {
            transparent|=pixels[i]==0;opaque|=pixels[i]==255;
        }
        assert(transparent && opaque);
        png_image_free(&image);
    }
}
