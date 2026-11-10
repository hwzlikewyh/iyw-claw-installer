#ifndef _WIN32
#include "unix_support.h"
#include <sodium.h>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <array>
namespace setup {
namespace {
constexpr const char* PublicKey="RWQs3MShTUgMUqJIgj5NzBI/EZyDJcjPnIGgzNUuBvd21qtV152OjF9X";
std::vector<unsigned char> Decode(const std::string& text){
    if(text.size()>32768)throw std::runtime_error("签名数据过大。");
    std::vector<unsigned char> bytes(text.size()+1);size_t count=0;
    if(sodium_base642bin(bytes.data(),bytes.size(),text.data(),text.size(),"\r\n",&count,nullptr,
        sodium_base64_VARIANT_ORIGINAL)!=0)throw std::runtime_error("签名编码无效。");
    bytes.resize(count);return bytes;
}
void CheckSignature(const Release& release,const std::array<unsigned char,64>& digest){
    const auto text=Decode(release.signature);std::istringstream lines(std::string(text.begin(),text.end()));
    std::string untrusted,encoded,trusted,global;
    std::getline(lines,untrusted);std::getline(lines,encoded);std::getline(lines,trusted);std::getline(lines,global);
    for(auto* line:{&encoded,&trusted,&global})if(!line->empty()&&line->back()=='\r')line->pop_back();
    const auto key=Decode(PublicKey),signature=Decode(encoded),commentSignature=Decode(global);
    if(key.size()!=42||signature.size()!=74||commentSignature.size()!=64||signature[0]!='E'||signature[1]!='D'||
        sodium_memcmp(key.data()+2,signature.data()+2,8)||trusted.rfind("trusted comment: ",0)!=0)
        throw std::runtime_error("安装包签名格式或发布密钥不匹配。");
    if(crypto_sign_verify_detached(signature.data()+10,digest.data(),digest.size(),key.data()+10)!=0)
        throw std::runtime_error("安装包发布签名校验失败。");
    std::vector<unsigned char> comment(signature.begin()+10,signature.end());
    const auto body=trusted.substr(17);comment.insert(comment.end(),body.begin(),body.end());
    if(crypto_sign_verify_detached(commentSignature.data(),comment.data(),comment.size(),key.data()+10)!=0)
        throw std::runtime_error("安装包签名附注验证失败。");
}
}
void VerifyDownload(const Release& release,FileWork& work){
    BeginStage(work,Phase::Verifying);
    if(sodium_init()<0)throw std::runtime_error("无法初始化签名校验。");
    work.progress.phaseTotal=release.size;work.progress.detail="校验 SHA-256 与原助理发布签名";
    std::ifstream file(work.staging/"setup.exe",std::ios::binary);if(!file)throw std::runtime_error("无法读取安装包。");
    crypto_hash_sha256_state sha;crypto_hash_sha256_init(&sha);
    crypto_generichash_state blake;crypto_generichash_init(&blake,nullptr,0,64);
    std::vector<unsigned char> bytes(MiB);
    while(file.read(reinterpret_cast<char*>(bytes.data()),bytes.size())||file.gcount()){
        CheckCancel(work.job);const auto count=static_cast<std::size_t>(file.gcount());
        crypto_hash_sha256_update(&sha,bytes.data(),count);crypto_generichash_update(&blake,bytes.data(),count);Advance(work,count);
    }
    unsigned char hash[32];crypto_hash_sha256_final(&sha,hash);char hex[65];sodium_bin2hex(hex,sizeof(hex),hash,sizeof(hash));
    if(!file.eof()||work.progress.phaseDone!=release.size||hex!=release.sha256)throw std::runtime_error("安装包 SHA-256 不匹配。");
    std::array<unsigned char,64> digest{};crypto_generichash_final(&blake,digest.data(),digest.size());
    CheckSignature(release,digest);work.progress.events.push_back("SHA-256 与原助理发布签名均通过。");
}
std::string HashFile(const fs::path& path){
    if(sodium_init()<0)throw std::runtime_error("无法初始化摘要计算。");
    std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("无法读取缓存文件。");
    crypto_hash_sha256_state hash;crypto_hash_sha256_init(&hash);std::vector<unsigned char> bytes(MiB);
    while(file.read(reinterpret_cast<char*>(bytes.data()),bytes.size())||file.gcount())
        crypto_hash_sha256_update(&hash,bytes.data(),static_cast<unsigned long long>(file.gcount()));
    if(!file.eof())throw std::runtime_error("缓存文件读取失败。");
    unsigned char digest[32];crypto_hash_sha256_final(&hash,digest);char text[65];sodium_bin2hex(text,sizeof(text),digest,sizeof(digest));return text;
}
}
#endif
