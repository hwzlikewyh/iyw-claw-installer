#include "payload.h"
#include <sodium.h>
#include <fstream>
#include <sstream>
#include <stdexcept>
namespace setup {
namespace {
constexpr const char* PublicKey="RWQs3MShTUgMUqJIgj5NzBI/EZyDJcjPnIGgzNUuBvd21qtV152OjF9X";
std::vector<unsigned char> Decode(const std::string& text) {
    if(text.empty() || text.size()>32768) throw std::runtime_error("应用载荷签名长度无效。");
    std::vector<unsigned char> bytes(text.size()+1); size_t count=0;
    if(sodium_base642bin(bytes.data(),bytes.size(),text.data(),text.size(),"\r\n",&count,nullptr,sodium_base64_VARIANT_ORIGINAL)!=0)
        throw std::runtime_error("应用载荷签名编码无效。");
    bytes.resize(count); return bytes;
}
void VerifyDetached(const std::string& signatureText, const fs::path& package) {
    const auto decoded=Decode(signatureText);
    std::istringstream lines(std::string(decoded.begin(),decoded.end()));
    std::string untrusted,encoded,trusted,global;
    std::getline(lines,untrusted);std::getline(lines,encoded);std::getline(lines,trusted);std::getline(lines,global);
    for(auto* line:{&encoded,&trusted,&global}) if(!line->empty()&&line->back()=='\r')line->pop_back();
    const auto key=Decode(PublicKey),signature=Decode(encoded),comment=Decode(global);
    if(key.size()!=42 || signature.size()!=74 || comment.size()!=64 || signature[0]!='E' || signature[1]!='D' ||
       sodium_memcmp(key.data()+2,signature.data()+2,8) || trusted.rfind("trusted comment: ",0)!=0)
        throw std::runtime_error("载荷签名格式或发布密钥不匹配。");
    crypto_generichash_state hash{};crypto_generichash_init(&hash,nullptr,0,64);
    std::ifstream file(package,std::ios::binary);std::vector<unsigned char> buffer(MiB);
    if(!file)throw std::runtime_error("无法读取应用载荷。");
    while(file.read(reinterpret_cast<char*>(buffer.data()),buffer.size())||file.gcount())
        crypto_generichash_update(&hash,buffer.data(),static_cast<std::size_t>(file.gcount()));
    std::array<unsigned char,64> digest{};crypto_generichash_final(&hash,digest.data(),digest.size());
    if(crypto_sign_verify_detached(signature.data()+10,digest.data(),digest.size(),key.data()+10)!=0)
        throw std::runtime_error("应用载荷发布签名校验失败。");
    std::vector<unsigned char> signedComment(signature.begin()+10,signature.end());
    const auto text=trusted.substr(17);signedComment.insert(signedComment.end(),text.begin(),text.end());
    if(crypto_sign_verify_detached(comment.data(),signedComment.data(),signedComment.size(),key.data()+10)!=0)
        throw std::runtime_error("应用载荷签名附注校验失败。");
}
}
void VerifyPayload(const Release& release, FileWork& work) {
    if(sodium_init()<0)throw std::runtime_error("无法初始化载荷验签。");
    std::ifstream file(work.staging/"setup.exe",std::ios::binary);
    if(!file)throw std::runtime_error("无法读取应用载荷。");
    std::vector<unsigned char> buffer(MiB);std::uint64_t total=0;
    while(file.read(reinterpret_cast<char*>(buffer.data()),buffer.size())||file.gcount()) {
        CheckCancel(work.job);const auto count=static_cast<std::size_t>(file.gcount());total+=count;
        if(total>release.size)throw std::runtime_error("应用载荷大小超出发布记录。");
        (void)count;
    }
    if(!file.eof()||total!=release.size)throw std::runtime_error("应用载荷未完整读取。");
    VerifyDetached(release.signature,work.staging/"setup.exe");
    work.progress.events.push_back("应用载荷 SHA-256 和固定发布密钥签名校验通过。");
}
}
