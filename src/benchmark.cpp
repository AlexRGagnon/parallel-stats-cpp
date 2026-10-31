#include <parallel_stats/statistics.hpp>
#include <charconv>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>
#include <string_view>
using namespace parallel_stats;
std::size_t parse(const char* text,std::size_t low,std::size_t high){std::string_view s(text);std::size_t v=0;auto r=std::from_chars(s.data(),s.data()+s.size(),v);if(r.ec!=std::errc{}||r.ptr!=s.data()+s.size()||v<low||v>high)throw std::invalid_argument("argument out of range");return v;}
int main(int argc,char** argv) {
    try {
        if(argc>4)throw std::invalid_argument("usage: stats-benchmark [count 1..20000000] [workers 1..64] [repeats 1..50]");
        const auto n=argc>1?parse(argv[1],1,20000000):2000000;
        const auto workers=argc>2?parse(argv[2],1,64):4;
        const auto repeats=argc>3?parse(argv[3],1,50):7;
        constexpr std::size_t chunk=16384,capacity=8;
        std::mt19937_64 rng(17);std::vector<double> data(n);for(auto& x:data)x=10000.0+static_cast<double>(rng()%1000000)/100000.0;
        auto expected=serial_chunks(data,chunk);auto warm=parallel_summarize(data,workers,chunk,capacity);
        if(warm.mean!=expected.mean||warm.m2!=expected.m2)throw std::runtime_error("numerical mismatch");
        std::vector<double> serial_ms,parallel_ms;
        auto measure=[&](bool parallel){auto start=std::chrono::steady_clock::now();auto m=parallel?parallel_summarize(data,workers,chunk,capacity):serial_chunks(data,chunk);auto end=std::chrono::steady_clock::now();if(m.count!=n||m.mean!=expected.mean||m.m2!=expected.m2)throw std::runtime_error("benchmark correctness failure");return std::chrono::duration<double,std::milli>(end-start).count();};
        for(std::size_t i=0;i<repeats;++i){if(i%2==0){serial_ms.push_back(measure(false));parallel_ms.push_back(measure(true));}else{parallel_ms.push_back(measure(true));serial_ms.push_back(measure(false));}}
        auto median=[](std::vector<double> values){std::sort(values.begin(),values.end());auto k=values.size()/2;return values.size()%2?values[k]:(values[k-1]+values[k])/2;};
        const auto serial=median(serial_ms),parallel=median(parallel_ms);
        auto array=[](const std::vector<double>& v){std::cout<<'[';for(std::size_t i=0;i<v.size();++i){if(i)std::cout<<',';std::cout<<v[i];}std::cout<<']';};
        std::cout<<std::setprecision(12)<<"{\n  \"observations\":"<<n<<",\n  \"workers\":"<<workers<<",\n  \"hardware_threads\":"<<std::thread::hardware_concurrency()<<",\n  \"queue_capacity\":"<<capacity<<",\n  \"chunk_size\":"<<chunk<<",\n  \"seed\":17,\n  \"repeats\":"<<repeats<<",\n  \"serial_ms\":";array(serial_ms);
        std::cout<<",\n  \"parallel_ms\":";array(parallel_ms);
        std::cout<<",\n  \"serial_median_ms\":"<<serial<<",\n  \"parallel_median_ms\":"<<parallel<<",\n  \"speedup\":"<<serial/parallel<<",\n  \"mean\":"<<expected.mean<<",\n  \"population_variance\":"<<expected.variance()<<",\n  \"exact_ordered_match\":true\n}\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
