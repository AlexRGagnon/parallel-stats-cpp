#include <parallel_stats/statistics.hpp>
#include <atomic>
#include <chrono>
#include <iostream>
#include <latch>
#include <map>
#include <numeric>
#include <string>
using namespace parallel_stats;
using namespace std::chrono_literals;
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<class F> void throws(F f) { bool raised=false; try { f(); } catch(const std::exception&) { raised=true; } check(raised,"expected exception"); }
void bounds() { throws([]{BoundedPool p(0,1);}); throws([]{BoundedPool p(1,0);}); throws([]{BoundedPool p(257,1);}); throws([]{BoundedPool p(1,1000001);}); }
void values() { BoundedPool p(2,2); auto a=p.submit([n=std::make_unique<int>(42)]{return *n;}); auto b=p.submit([]{}); check(a.get()==42,"move-only callable"); b.get(); }
void exception() { BoundedPool p(1,1); auto a=p.submit([]()->int{throw std::runtime_error("task");}); throws([&]{a.get();}); check(p.submit([]{return 9;}).get()==9,"worker survived exception"); }
void drain() { std::atomic<int> n=0; std::vector<std::future<void>> f; { BoundedPool p(3,4); for(int i=0;i<100;++i) f.push_back(p.submit([&]{++n;})); } for(auto& v:f)v.get(); check(n==100,"destructor drained"); }
void closed() { BoundedPool p(1,1); p.close(); p.close(); throws([&]{p.submit([]{});}); check(p.snapshot().closed,"closed flag"); }
void blocked_close() {
    BoundedPool p(1,1); std::promise<void> release; auto gate=release.get_future().share(); std::latch entered(1);
    auto a=p.submit([&]{entered.count_down(); gate.wait();}); entered.wait(); auto b=p.submit([]{});
    auto c=std::async(std::launch::async,[&]{try{p.submit([]{}); return false;}catch(const std::runtime_error&){return true;}});
    p.close(); const auto status=c.wait_for(2s); release.set_value(); a.get(); b.get();
    check(status==std::future_status::ready && c.get(),"close unblocks submitter");
}
void backpressure() {
    BoundedPool p(1,1); std::promise<void> release; auto gate=release.get_future().share(); std::latch entered(1);
    auto a=p.submit([&]{entered.count_down(); gate.wait();}); entered.wait(); auto b=p.submit([]{return 2;});
    auto c=std::async(std::launch::async,[&]{return p.submit([]{return 3;}).get();});
    const auto status=c.wait_for(50ms); const auto s=p.snapshot(); release.set_value();
    a.get(); check(b.get()==2 && c.get()==3,"blocked submit completed");
    check(status==std::future_status::timeout && s.queued==1 && s.peak_queued==1,"bounded backlog");
}
void reentrant() { BoundedPool p(1,1); auto f=p.submit([&]{throws([&]{p.submit([]{});});}); f.get(); }
void producers() {
    BoundedPool p(4,3); std::atomic<int> count=0; std::vector<std::jthread> producers;
    for(int t=0;t<8;++t) producers.emplace_back([&]{std::vector<std::future<void>> f;for(int i=0;i<100;++i)f.push_back(p.submit([&]{++count;}));for(auto& a:f)a.get();});
    producers.clear(); auto s=p.snapshot(); check(count==800 && s.peak_active<=4 && s.peak_queued<=3,"multi-producer invariants");
}
void moments() {
    std::vector<double> x{1,2,3,4}; auto m=summarize(x); check(m.count==4 && m.mean==2.5L && m.variance()==1.25L,"known population moments");
    check(std::abs(m.variance(true)-5.0L/3.0L)<1e-12L,"sample variance");
    std::vector<double> offset{1e12+1,1e12+2,1e12+3,1e12+4}; check(std::abs(summarize(offset).variance()-1.25L)<1e-9L,"large offset");
    Moments empty; throws([&]{empty.variance();}); auto one=summarize(std::span<const double>(x.data(),1)); throws([&]{one.variance(true);});
}
void determinism() {
    std::vector<double> x(20003); for(std::size_t i=0;i<x.size();++i)x[i]=static_cast<double>((i*71)%1000)/17.0;
    auto expected=serial_chunks(x,137); for(std::size_t workers:{1U,2U,4U,8U}) {auto got=parallel_summarize(x,workers,137,2); check(got.count==expected.count&&got.mean==expected.mean&&got.m2==expected.m2,"fixed-order reduction");}
    check(parallel_summarize({}).count==0,"empty reduction");
}
void nonfinite() { std::vector<double> x{1,2,std::numeric_limits<double>::quiet_NaN(),4}; throws([&]{parallel_summarize(x,2,2,1);}); x[2]=std::numeric_limits<double>::infinity(); throws([&]{summarize(x);}); throws([&]{parallel_summarize(x,1,0,1);}); }
void stress() { for(int repeat=0;repeat<20;++repeat){BoundedPool p(4,2);std::vector<std::future<int>> f;for(int i=0;i<200;++i)f.push_back(p.submit([i]{return i*i;}));for(int i=0;i<200;++i)check(f[static_cast<std::size_t>(i)].get()==i*i,"stress result");} }
int main(int argc,char** argv) {
    const std::map<std::string,void(*)()> cases{{"bounds",bounds},{"values",values},{"exception",exception},{"drain",drain},{"closed",closed},{"blocked_close",blocked_close},{"backpressure",backpressure},{"reentrant",reentrant},{"producers",producers},{"moments",moments},{"determinism",determinism},{"nonfinite",nonfinite},{"stress",stress}};
    try {if(argc!=2 || !cases.contains(argv[1]))throw std::invalid_argument("specify one test name");cases.at(argv[1])();std::cout<<"PASS "<<argv[1]<<'\n';return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
