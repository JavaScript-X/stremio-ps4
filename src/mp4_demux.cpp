#include "mp4_demux.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
uint32_t be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
        (uint32_t(p[2]) << 8) | p[3];
}
uint64_t be64(const uint8_t* p) {
    return (uint64_t(be32(p)) << 32) | be32(p + 4);
}
bool readAt(FILE* f, uint64_t at, void* out, size_t size) {
    return at <= 0x7fffffff && std::fseek(f, static_cast<long>(at), SEEK_SET) == 0 &&
        std::fread(out, 1, size, f) == size;
}
struct Atom { uint64_t at = 0, size = 0, data = 0; char type[5] = {}; };
bool atomAt(FILE* f, uint64_t at, uint64_t limit, Atom& a) {
    uint8_t h[16];
    if (at + 8 > limit || !readAt(f, at, h, 8)) return false;
    uint64_t size = be32(h);
    size_t header = 8;
    if (size == 1) {
        if (at + 16 > limit || !readAt(f, at + 8, h + 8, 8)) return false;
        size = be64(h + 8); header = 16;
    } else if (size == 0) size = limit - at;
    if (size < header || at + size > limit) return false;
    a.at = at; a.size = size; a.data = at + header;
    std::memcpy(a.type, h + 4, 4); a.type[4] = 0;
    return true;
}
bool child(FILE* f, const Atom& parent, const char* type, Atom& out,
        uint64_t skip = 0) {
    const uint64_t end = parent.at + parent.size;
    for (uint64_t at = parent.data + skip; at + 8 <= end;) {
        Atom a; if (!atomAt(f, at, end, a)) return false;
        if (std::memcmp(a.type, type, 4) == 0) { out = a; return true; }
        at += a.size;
    }
    return false;
}
bool loadPayload(FILE* f, const Atom& a, std::vector<uint8_t>& data,
        size_t maximum = 32 * 1024 * 1024) {
    const uint64_t n = a.at + a.size - a.data;
    if (n > maximum) return false;
    data.resize(static_cast<size_t>(n));
    return readAt(f, a.data, data.data(), data.size());
}
struct Stsc { uint32_t first = 0, count = 0; };
struct Track {
    Atom stbl;
    uint32_t timescale = 0;
    uint64_t duration = 0;
    uint32_t width = 0, height = 0;
    uint8_t nalBytes = 0;
    std::vector<std::vector<uint8_t>> parameterSets;
    std::vector<uint32_t> sizes;
    std::vector<uint64_t> chunks;
    std::vector<Stsc> mapping;
    std::vector<uint32_t> sync;
    uint64_t sampleDurationTotal = 0;
};
bool parseAvcc(FILE* f, const Atom& avcc, Track& t) {
    std::vector<uint8_t> d; if (!loadPayload(f, avcc, d, 1024 * 1024) || d.size() < 7) return false;
    t.nalBytes = (d[4] & 3) + 1;
    size_t p = 6; int count = d[5] & 31;
    for (int group = 0; group < 2; ++group) {
        if (group) { if (p >= d.size()) return false; count = d[p++]; }
        for (int i = 0; i < count; ++i) {
            if (p + 2 > d.size()) return false;
            const size_t n = (d[p] << 8) | d[p + 1]; p += 2;
            if (!n || p + n > d.size()) return false;
            t.parameterSets.emplace_back(d.begin() + p, d.begin() + p + n); p += n;
        }
    }
    return !t.parameterSets.empty();
}
bool parseVideoTrack(FILE* f, const Atom& trak, Track& t) {
    Atom mdia, hdlr, minf, stbl, stsd, mdhd;
    if (!child(f, trak, "mdia", mdia) || !child(f, mdia, "hdlr", hdlr) ||
        !child(f, mdia, "minf", minf) || !child(f, minf, "stbl", stbl) ||
        !child(f, stbl, "stsd", stsd) || !child(f, mdia, "mdhd", mdhd)) return false;
    uint8_t handler[12]; if (!readAt(f, hdlr.data, handler, sizeof(handler)) ||
        std::memcmp(handler + 8, "vide", 4) != 0) return false;
    uint8_t mh[32]; if (!readAt(f, mdhd.data, mh, sizeof(mh))) return false;
    const bool v1 = mh[0] == 1;
    t.timescale = be32(mh + (v1 ? 20 : 12));
    t.duration = v1 ? be64(mh + 24) : be32(mh + 16);
    uint8_t sd[16]; if (!readAt(f, stsd.data, sd, sizeof(sd)) || be32(sd + 4) < 1) return false;
    Atom entry; if (!atomAt(f, stsd.data + 8, stsd.at + stsd.size, entry) ||
        (std::memcmp(entry.type, "avc1", 4) && std::memcmp(entry.type, "avc3", 4))) return false;
    uint8_t visual[28]; if (!readAt(f, entry.data, visual, sizeof(visual))) return false;
    t.width = (visual[24] << 8) | visual[25]; t.height = (visual[26] << 8) | visual[27];
    Atom avcc; if (!child(f, entry, "avcC", avcc, 78) || !parseAvcc(f, avcc, t)) return false;
    t.stbl = stbl; return true;
}
bool table32(FILE* f, const Atom& a, std::vector<uint32_t>& out, bool stsz) {
    uint8_t h[12]; if (!readAt(f, a.data, h, sizeof(h))) return false;
    uint32_t count = be32(h + (stsz ? 8 : 4));
    if (count > 2000000) return false;
    out.resize(count);
    if (stsz && be32(h + 4)) { std::fill(out.begin(), out.end(), be32(h + 4)); return true; }
    const uint64_t start = a.data + (stsz ? 12 : 8);
    std::vector<uint8_t> raw(size_t(count) * 4);
    if (!readAt(f, start, raw.data(), raw.size())) return false;
    for (uint32_t i = 0; i < count; ++i) out[i] = be32(raw.data() + size_t(i) * 4);
    return true;
}
bool parseTables(FILE* f, Track& t) {
    Atom a;
    if (!child(f, t.stbl, "stsz", a) || !table32(f, a, t.sizes, true)) return false;
    if (child(f, t.stbl, "stco", a)) {
        std::vector<uint32_t> c; if (!table32(f, a, c, false)) return false;
        t.chunks.assign(c.begin(), c.end());
    } else if (child(f, t.stbl, "co64", a)) {
        uint8_t h[8]; if (!readAt(f, a.data, h, 8)) return false;
        const uint32_t n = be32(h + 4); if (n > 2000000) return false;
        std::vector<uint8_t> raw(size_t(n) * 8); if (!readAt(f, a.data + 8, raw.data(), raw.size())) return false;
        t.chunks.resize(n); for (uint32_t i=0;i<n;++i) t.chunks[i]=be64(raw.data()+size_t(i)*8);
    } else return false;
    if (!child(f, t.stbl, "stsc", a)) return false;
    uint8_t h[8]; if (!readAt(f, a.data, h, 8)) return false;
    uint32_t n = be32(h + 4); if (!n || n > 100000) return false;
    std::vector<uint8_t> raw(size_t(n) * 12); if (!readAt(f, a.data + 8, raw.data(), raw.size())) return false;
    for (uint32_t i=0;i<n;++i) t.mapping.push_back({be32(raw.data()+size_t(i)*12),be32(raw.data()+size_t(i)*12+4)});
    if (child(f, t.stbl, "stss", a)) table32(f, a, t.sync, false);
    if (child(f, t.stbl, "stts", a) && readAt(f, a.data, h, 8)) {
        n = be32(h + 4); if (n < 100000) {
            raw.resize(size_t(n) * 8);
            if (readAt(f, a.data + 8, raw.data(), raw.size()))
                for(uint32_t i=0;i<n;++i) t.sampleDurationTotal += uint64_t(be32(raw.data()+i*8))*be32(raw.data()+i*8+4);
        }
    }
    return !t.sizes.empty() && !t.chunks.empty();
}
bool writeStart(FILE* out, const std::vector<uint8_t>& nal) {
    static const uint8_t sc[4] = {0,0,0,1};
    return std::fwrite(sc,1,4,out)==4 && std::fwrite(nal.data(),1,nal.size(),out)==nal.size();
}
}

int demuxMp4AvcToAnnexB(const std::string& inputPath,
    const std::string& outputPath, Mp4MediaInfo& info) {
    info = {};
    FILE* in = std::fopen(inputPath.c_str(), "rb"); if (!in) return 1;
    std::fseek(in,0,SEEK_END); long length=std::ftell(in); if(length<16){std::fclose(in);return 2;}
    Atom moov; bool found=false;
    for(uint64_t at=0;at+8<=uint64_t(length);){Atom a;if(!atomAt(in,at,length,a))break;if(!std::memcmp(a.type,"moov",4)){moov=a;found=true;break;}at+=a.size;}
    if(!found){std::fclose(in);return 3;}
    Track t; const uint64_t end=moov.at+moov.size;
    for(uint64_t at=moov.data;at+8<=end;){Atom a;if(!atomAt(in,at,end,a))break;if(!std::memcmp(a.type,"trak",4)&&parseVideoTrack(in,a,t)){found=true;break;}at+=a.size;}
    if(t.nalBytes==0){std::fclose(in);return 4;}
    if(!parseTables(in,t)){std::fclose(in);return 5;}
    FILE* out=std::fopen(outputPath.c_str(),"wb");if(!out){std::fclose(in);return 6;}
    static const uint8_t aud[]={0,0,0,1,9,0x10};
    std::vector<uint8_t> sample; size_t sampleIndex=0, mapIndex=0; bool ok=true;
    for(size_t chunk=1;chunk<=t.chunks.size()&&sampleIndex<t.sizes.size()&&ok;++chunk){
        while(mapIndex+1<t.mapping.size()&&t.mapping[mapIndex+1].first<=chunk)++mapIndex;
        uint64_t pos=t.chunks[chunk-1];
        for(uint32_t j=0;j<t.mapping[mapIndex].count&&sampleIndex<t.sizes.size();++j,++sampleIndex){
            const uint32_t n=t.sizes[sampleIndex]; sample.resize(n);
            if(!readAt(in,pos,sample.data(),n)){ok=false;break;} pos+=n;
            if(std::fwrite(aud,1,sizeof(aud),out)!=sizeof(aud)){ok=false;break;}
            const bool sync=t.sync.empty()||std::binary_search(t.sync.begin(),t.sync.end(),uint32_t(sampleIndex+1));
            if((sampleIndex==0||sync)) for(const auto& ps:t.parameterSets) if(!writeStart(out,ps)){ok=false;break;}
            size_t p=0;
            while(ok&&p+t.nalBytes<=sample.size()){
                uint32_t bytes=0;for(uint8_t b=0;b<t.nalBytes;++b)bytes=(bytes<<8)|sample[p+b];p+=t.nalBytes;
                if(!bytes||p+bytes>sample.size()){ok=false;break;}
                std::vector<uint8_t> nal(sample.begin()+p,sample.begin()+p+bytes);ok=writeStart(out,nal);p+=bytes;
            }
            if(p!=sample.size())ok=false;
        }
    }
    const bool closeOk=std::fclose(out)==0;std::fclose(in);
    if(!ok||!closeOk||sampleIndex!=t.sizes.size()){std::remove(outputPath.c_str());return 7;}
    info.width=t.width;info.height=t.height;info.samples=uint32_t(t.sizes.size());info.videoCodec="H.264/AVC";
    const uint64_t ticks=t.sampleDurationTotal?t.sampleDurationTotal:t.duration;
    info.durationMs=t.timescale?ticks*1000/t.timescale:0;
    info.fpsTimes100=ticks?uint32_t(uint64_t(t.sizes.size())*t.timescale*100/ticks):0;
    return 0;
}
