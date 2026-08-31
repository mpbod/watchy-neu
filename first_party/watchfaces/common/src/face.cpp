#include "watchy_first_party/face.h"
namespace watchy_first_party { namespace {
bool leap(int y) noexcept { return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0); }
uint8_t dim(int y,uint8_t m) noexcept { static const uint8_t d[]={31,28,31,30,31,30,31,31,30,31,30,31}; return d[m-1]+(m==2&&leap(y)); }
void put2(char *p,uint8_t v) noexcept { p[0]=char('0'+v/10); p[1]=char('0'+v%10); }
int64_t days(const watchy_time_t&t) noexcept { int y=t.year,m=t.month;y-=m<=2;int era=(y>=0?y:y-399)/400;unsigned yoe=unsigned(y-era*400);unsigned doy=(153*(m+(m>2?-3:9))+2)/5+t.day-1;unsigned doe=yoe*365+yoe/4-yoe/100+doy;return int64_t(era)*146097+int64_t(doe)-719468; }
uint8_t weekday_for(const watchy_time_t&t) noexcept { int64_t v=(days(t)+4)%7;if(v<0)v+=7;return uint8_t(v); }
}
bool valid_time(const watchy_time_t&t) noexcept { return t.year>=1&&t.year<=9999&&t.month>=1&&t.month<=12&&t.day>=1&&t.day<=dim(t.year,t.month)&&t.hour<24&&t.minute<60&&t.second<60&&t.utc_offset_minutes>=-720&&t.utc_offset_minutes<=840; }
bool format_hhmm_checked(const watchy_time_t&t,bool twelve,fixed_text*out) noexcept { if(!out||!valid_time(t))return false;uint8_t h=t.hour;if(twelve){h=uint8_t(h%12);if(!h)h=12;}put2(out->value,h);out->value[2]=':';put2(out->value+3,t.minute);out->value[5]='\0';return true; }
fixed_text format_hhmm(const watchy_time_t&t,bool twelve) noexcept { fixed_text r;(void)format_hhmm_checked(t,twelve,&r);return r; }
fixed_text format_date(const watchy_time_t&t) noexcept { fixed_text r;if(!valid_time(t))return r;r.value[0]=char('0'+t.month/10);r.value[1]=char('0'+t.month%10);r.value[2]='/';put2(r.value+3,t.day);r.value[5]='/';r.value[6]=char('0'+(t.year/1000)%10);r.value[7]=char('0'+(t.year/100)%10);r.value[8]=char('0'+(t.year/10)%10);r.value[9]=char('0'+t.year%10);r.value[10]='\0';return r; }
fixed_text format_weekday(const watchy_time_t&t) noexcept { static const char*n[]={"SUN","MON","TUE","WED","THU","FRI","SAT"};return valid_time(t)?fixed_text(n[weekday_for(t)]):fixed_text(); }
bool offset_time_checked(const watchy_time_t&t,int16_t target,watchy_time_t*out) noexcept { if(!out||!valid_time(t)||target<-720||target>840)return false;watchy_time_t r=t;int total=t.hour*60+t.minute+target-t.utc_offset_minutes,delta=0;while(total<0){total+=1440;--delta;}while(total>=1440){total-=1440;++delta;}r.hour=uint8_t(total/60);r.minute=uint8_t(total%60);r.utc_offset_minutes=target;while(delta<0){if(r.day==1){if(r.month==1){if(r.year==1)return false;--r.year;r.month=12;}else --r.month;r.day=dim(r.year,r.month);}else --r.day;++delta;}while(delta>0){if(r.day==dim(r.year,r.month)){r.day=1;if(r.month==12){if(r.year==9999)return false;++r.year;r.month=1;}else ++r.month;}else ++r.day;--delta;}r.weekday=weekday_for(r);*out=r;return true; }
watchy_time_t offset_time(const watchy_time_t&t,int16_t target) noexcept { watchy_time_t r{};(void)offset_time_checked(t,target,&r);return r; }
uint8_t moon_octant(const watchy_time_t&t) noexcept { if(!valid_time(t))return 0;constexpr int64_t epoch_day=10962,epoch_minute=1094,cycle=42524;int64_t minute=(days(t)-epoch_day)*1440+t.hour*60+t.minute-epoch_minute;int64_t phase=minute%cycle;if(phase<0)phase+=cycle;return uint8_t((phase*8)/cycle); }
}
