#ifndef _WIN32
#include "release.h"
#include <curl/curl.h>
#include <memory>
#include <stdexcept>
#include <cctype>
#include <sstream>
namespace setup {
namespace {
struct Transfer {const Receive& receive;InstallJob& job;std::exception_ptr error;std::string location;long status=0;bool ranged=false;std::string range;};
size_t Body(char* bytes,size_t size,size_t count,void* context){
    auto& transfer=*static_cast<Transfer*>(context);
    if(transfer.ranged&&transfer.status==200)return 0;
    if(transfer.status!=(transfer.ranged?206:200))return size*count;
    try{CheckCancel(transfer.job);transfer.receive(bytes,size*count);return size*count;}
    catch(...){transfer.error=std::current_exception();return 0;}
}
size_t Header(char* bytes,size_t size,size_t count,void* context){
    auto& transfer=*static_cast<Transfer*>(context);std::string line(bytes,size*count);
    if(line.rfind("HTTP/",0)==0){std::istringstream status(line);std::string protocol;status>>protocol>>transfer.status;}
    auto colon=line.find(':');if(colon==std::string::npos)return size*count;
    auto name=line.substr(0,colon);for(auto& c:name)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if(name=="location"){
        const auto start=line.find_first_not_of(" \t",colon+1),end=line.find_last_not_of("\r\n \t");
        if(start!=std::string::npos&&end>=start)transfer.location=line.substr(start,end-start+1);
    }
    if(name=="content-range"){
        const auto start=line.find_first_not_of(" \t",colon+1),end=line.find_last_not_of("\r\n \t");
        if(start!=std::string::npos&&end>=start)transfer.range=line.substr(start,end-start+1);
    }
    return size*count;
}
int Progress(void* context,curl_off_t,curl_off_t,curl_off_t,curl_off_t){return static_cast<Transfer*>(context)->job.IsCancelled()?1:0;}
void ValidateURL(const std::string& url){
    if(url.rfind("https://",0)!=0)throw std::runtime_error("下载地址必须使用 HTTPS。");
    const auto host=url.substr(8,url.find('/',8)-8);
    const auto suffix=std::string(".volces.com");
    const bool tos=host.size()>suffix.size()&&host.compare(host.size()-suffix.size(),suffix.size(),suffix)==0&&host.find(".tos-")!=std::string::npos;
    if(host!="gateway.iyw.cn"&&host!="vol-ai.iywtu.com"&&!tos)throw std::runtime_error("下载域名不受信任。");
}
}
bool TransferHttps(const std::string& url,const Receive& receive,InstallJob& job,
    const std::pair<std::uint64_t,std::uint64_t>* range,const std::string* body=nullptr){
    static const auto initialized=curl_global_init(CURL_GLOBAL_DEFAULT);
    if(initialized!=CURLE_OK)throw std::runtime_error("无法初始化系统网络服务。");
    auto current=url;
    for(unsigned redirect=0;redirect<6;++redirect){
        CheckCancel(job);ValidateURL(current);Transfer transfer{receive,job,{},{}};transfer.ranged=range!=nullptr;
        std::unique_ptr<CURL,decltype(&curl_easy_cleanup)> request(curl_easy_init(),curl_easy_cleanup);
        if(!request)throw std::runtime_error("无法创建下载请求。");
        curl_slist* raw=nullptr;raw=curl_slist_append(raw,"Cache-Control: no-cache");
        raw=curl_slist_append(raw,"X-IYW-Installation-ID: yuan-assistant-installer");
        if(body)raw=curl_slist_append(raw,"Content-Type: application/json");
        const auto rangeText=range?"Range: bytes="+std::to_string(range->first)+"-"+std::to_string(range->second):"";
        if(range)raw=curl_slist_append(raw,rangeText.c_str());
        std::unique_ptr<curl_slist,decltype(&curl_slist_free_all)> headers(raw,curl_slist_free_all);
        curl_easy_setopt(request.get(),CURLOPT_URL,current.c_str());curl_easy_setopt(request.get(),CURLOPT_HTTPHEADER,headers.get());
        if(body){curl_easy_setopt(request.get(),CURLOPT_POSTFIELDS,body->data());curl_easy_setopt(request.get(),CURLOPT_POSTFIELDSIZE,static_cast<long>(body->size()));}
        curl_easy_setopt(request.get(),CURLOPT_FOLLOWLOCATION,0L);curl_easy_setopt(request.get(),CURLOPT_CONNECTTIMEOUT,15L);
        curl_easy_setopt(request.get(),CURLOPT_LOW_SPEED_LIMIT,1L);curl_easy_setopt(request.get(),CURLOPT_LOW_SPEED_TIME,30L);
        curl_easy_setopt(request.get(),CURLOPT_NOSIGNAL,1L);curl_easy_setopt(request.get(),CURLOPT_SSL_VERIFYPEER,1L);
        curl_easy_setopt(request.get(),CURLOPT_SSL_VERIFYHOST,2L);curl_easy_setopt(request.get(),CURLOPT_WRITEFUNCTION,Body);
        curl_easy_setopt(request.get(),CURLOPT_WRITEDATA,&transfer);curl_easy_setopt(request.get(),CURLOPT_HEADERFUNCTION,Header);
        curl_easy_setopt(request.get(),CURLOPT_HEADERDATA,&transfer);curl_easy_setopt(request.get(),CURLOPT_XFERINFOFUNCTION,Progress);
        curl_easy_setopt(request.get(),CURLOPT_XFERINFODATA,&transfer);curl_easy_setopt(request.get(),CURLOPT_NOPROGRESS,0L);
        const auto result=curl_easy_perform(request.get());if(transfer.error)std::rethrow_exception(transfer.error);
        if(range&&transfer.status==200)return false;
        if(result!=CURLE_OK){CheckCancel(job);throw std::runtime_error("下载失败："+std::string(curl_easy_strerror(result)));}
        long status=0;curl_easy_getinfo(request.get(),CURLINFO_RESPONSE_CODE,&status);
        if(status>=300&&status<=399){if(body)throw std::runtime_error("环境计划不允许重定向。");current=transfer.location;continue;}
        if(status!=(range?206:200))throw std::runtime_error("安装服务返回 HTTP "+std::to_string(status));
        if(range&&transfer.range.rfind("bytes "+std::to_string(range->first)+"-"+std::to_string(range->second)+"/",0)!=0)
            throw std::runtime_error("下载分段范围不匹配。");
        return true;
    }
    throw std::runtime_error("下载重定向过多。");
}
void ReadHttps(const std::string& url,const Receive& receive,InstallJob& job){TransferHttps(url,receive,job,nullptr);}
void PostHttps(const std::string& url,const std::string& body,const Receive& receive,InstallJob& job){TransferHttps(url,receive,job,nullptr,&body);}
bool ReadHttpsRange(const std::string& url,const std::pair<std::uint64_t,std::uint64_t>& range,const Receive& receive,InstallJob& job){
    return TransferHttps(url,receive,job,&range);
}
}
#endif
