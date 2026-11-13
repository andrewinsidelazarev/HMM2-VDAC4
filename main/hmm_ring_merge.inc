    out.clear();
    // Every cell already stores a monotonically ordered stream. Merge those
    // streams directly, breaking equal orders by the original y/x cell order.
    // No per-fragment source array or second radix array is needed.
    struct Stream {const std::vector<RingDraw> *source;unsigned at,order;uint16_t hero,run=0;int16_t x,y;};
    std::vector<Stream> streams;std::vector<uint16_t> heap;
    streams.reserve(unsigned(cols*rows));heap.reserve(unsigned(cols*rows));
    for(int y=first_y;y<first_y+rows;++y)for(int x=first_x;x<first_x+cols;++x){
        const auto *p=ring_cell(x,y);if(!p||!p->valid)continue;const auto &cell=*p;
        const unsigned tail=cell.frame_cached?cell.tail:0;if(tail==cell.source.size())continue;
        const unsigned base=heroes?unsigned(heroes->size()):0;
        if(heroes){unsigned run=0;
            for(unsigned j=tail;j<cell.source.size();){
                if(!ring_hero(cell.source[j])){++j;continue;}
                if(heroes->size()>=0xffff)throw std::bad_alloc();heroes->push_back({int16_t(x),int16_t(y),uint16_t(run++)});
                do{++j;}while(j<cell.source.size()&&ring_hero(cell.source[j]));
            }
        }
#ifdef HMM2_HOST
        for(unsigned j=tail+1;j<cell.source.size();++j)if(cell.source[j-1].order>cell.source[j].order)hmm_fail("cell source is not ordered");
#endif
        heap.push_back(uint16_t(streams.size()));
        streams.push_back({&cell.source,tail,cell.source[tail].order,uint16_t(base),0,int16_t(x*32),int16_t(y*32)});
    }
    auto earlier=[&](unsigned a,unsigned b){return streams[a].order!=streams[b].order?streams[a].order<streams[b].order:a<b;};
    auto sift=[&](unsigned at){const unsigned value=heap[at],count=unsigned(heap.size());
        for(unsigned child=at*2+1;child<count;child=at*2+1){
            if(child+1<count&&earlier(heap[child+1],heap[child]))++child;
            if(!earlier(heap[child],value))break;heap[at]=heap[child];at=child;
        }
        heap[at]=uint16_t(value);
    };
    {Prof sort_time(P_PARTS);for(unsigned i=unsigned(heap.size()/2);i--;)sift(i);
    while(!heap.empty()){
        auto &stream=streams[heap[0]];const auto &source=*stream.source;Draw d;
        if(heroes&&ring_hero(source[stream.at])){
            d.r={stream.x,stream.y,32,32};d.layer=d.raw=true;d.mask=255;d.mapping=stream.hero+stream.run++;
            do{++stream.at;}while(stream.at<source.size()&&ring_hero(source[stream.at]));
        }else{d=source[stream.at++];d.r.x+=stream.x;d.r.y+=stream.y;}
        out.push_back(std::move(d));
        if(stream.at==source.size()){heap[0]=heap.back();heap.pop_back();}
        else stream.order=source[stream.at].order;
        if(!heap.empty())sift(0);
    }
    }
