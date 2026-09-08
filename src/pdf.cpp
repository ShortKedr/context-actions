#include "core.h"
#include <wincodec.h>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace ca {
void createImagePdf(const fs::path& image, const fs::path& output) {
    ComPtr<IWICImagingFactory> factory;
    check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));
    ComPtr<IWICBitmapDecoder> decoder;
    check(factory->CreateDecoderFromFilename(image.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder));
    ComPtr<IWICBitmapFrameDecode> frame;check(decoder->GetFrame(0,&frame));
    UINT width=0,height=0;check(frame->GetSize(&width,&height));
    if(!width||!height)throw std::runtime_error("Empty image");
    // One JPEG XObject per page; qpdf handles page assembly. No fonts or text renderer needed.
    const double pageW=width>height?842:595,pageH=width>height?595:842;
    const double scale=std::min((pageW-48)/width,(pageH-48)/height),w=width*scale,h=height*scale;
    std::ostringstream content;content.imbue(std::locale::classic());
    content<<"q\n"<<w<<" 0 0 "<<h<<' '<<(pageW-w)/2<<' '<<(pageH-h)/2<<" cm\n/Im0 Do\nQ\n";
    std::ofstream pdf(output,std::ios::binary);pdf.imbue(std::locale::classic());
    pdf<<"%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
    std::vector<std::streamoff> offsets{0};
    auto object=[&](int id){offsets.push_back(pdf.tellp());pdf<<id<<" 0 obj\n";};
    object(1);pdf<<"<< /Type /Catalog /Pages 2 0 R >>\nendobj\n";
    object(2);pdf<<"<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n";
    object(3);pdf<<"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 "<<pageW<<' '<<pageH<<"] /Resources << /XObject << /Im0 4 0 R >> >> /Contents 5 0 R >>\nendobj\n";
    object(4);pdf<<"<< /Type /XObject /Subtype /Image /Width "<<width<<" /Height "<<height<<" /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode /Length "<<fs::file_size(image)<<" >>\nstream\n";
    std::ifstream jpeg(image,std::ios::binary);pdf<<jpeg.rdbuf();pdf<<"\nendstream\nendobj\n";
    object(5);pdf<<"<< /Length "<<content.str().size()<<" >>\nstream\n"<<content.str()<<"endstream\nendobj\n";
    auto xref=pdf.tellp();pdf<<"xref\n0 6\n0000000000 65535 f \n";
    for(size_t i=1;i<offsets.size();++i)pdf<<std::setw(10)<<std::setfill('0')<<offsets[i]<<" 00000 n \n";
    pdf<<"trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n"<<xref<<"\n%%EOF\n";
    pdf.flush();if(!pdf||!jpeg)throw std::runtime_error("Cannot write PDF");
}
}
