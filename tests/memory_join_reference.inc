void join_layers_reference(Sequence<Draw> &draws){
    // Codex: ImageRestorer возвращает части одного слоя. Склеиваем соседние
    // прямоугольники с тем же происхождением, сохраняя порядок перекрытий.
    Sequence<Draw> out;out.reserve(draws.size());
    // Codex: отрицательный ответ фильтра исключает прежний совместимый слой.
    // Коллизия лишь оставляет исходный поиск; биты не снимаются при слиянии/удалении.
    std::array<uint32_t,64> seen{};
    struct Background {const Bitmap *bitmap=nullptr;size_t at=0;};Background backgrounds[8];
    // Codex: копия Draw на каждый слой — атомарный счётчик shared_ptr; исходный список всё равно заменяется, поэтому перенос.
    // Поиск назад ограничен 16 совпадениями, проверка перекрытий — 64 слоями: на списке в тысячи записей они были квадратичны. Вне окна слои просто не склеиваются, картинка та же.
    for(auto &item:draws){Draw d=std::move(item);bool merged_early=false;
        // Codex: восстановление малого участка уже видимого непрозрачного фона
        // из того же Bitmap не добавляет пикселей. Чужое перекрытие запрещает пропуск.
        const auto *bitmap=d.bitmap.get();
        if(d.opaque&&bitmap&&!bitmap->frame&&bitmap->single&&bitmap->w*bitmap->h>=4096&&d.r.w*d.r.h<=1024){
            const auto &hint=backgrounds[(reinterpret_cast<uintptr_t>(bitmap)>>4)&7];
            if(hint.bitmap==bitmap&&hint.at<out.size()&&out.size()-hint.at<=64){
                const auto &base=out[hint.at];const auto hit=intersect(base.r,d.r);
                if(same_layer_source(base,d)&&hit.w==d.r.w&&hit.h==d.r.h){bool blocked=false;
                    for(size_t k=hint.at+1;k<out.size();++k){const auto v=intersect(out[k].r,d.r);if(v.w&&v.h&&!same_layer_source(out[k],d)){blocked=true;break;}}
                    if(!blocked)continue;
                }
            }
        }
        uint32_t hash=d.resource^uint32_t(reinterpret_cast<uintptr_t>(d.bitmap.get())>>4);
        hash=(hash^d.mapping)*16777619u;
        hash=(hash^uint32_t(d.step)^(unsigned(d.raw)|(unsigned(d.fill)<<1)|(unsigned(d.repeat)<<2)|(unsigned(d.opaque)<<3)))*16777619u;
        if(d.fill)hash=(hash^unsigned(d.color)^(unsigned(d.mask)<<8))*16777619u;
        else {hash=(hash^uint32_t(d.sx-d.r.x*d.step)^(unsigned(d.color)<<24))*16777619u;hash=(hash^uint32_t(d.sy-d.r.y))*16777619u;}
        hash^=hash>>16;hash*=0x7feb352du;hash^=hash>>15;
        const unsigned at=(hash>>5)&63;const uint32_t bit=1u<<(hash&31);const bool candidate=!d.layer&&(seen[at]&bit);seen[at]|=bit;
        for(size_t j=out.size(),checked=0;candidate&&j&&checked<16;++checked){const auto &a=out[--j];
            if(out.size()-j>64)break;
            if(!same_layer_source(a,d))continue;
            Rect r=d.r;
            if(a.r.x==r.x&&a.r.w==r.w&&(a.r.y+a.r.h==r.y||r.y+r.h==a.r.y)){
                r.y=std::min(r.y,a.r.y);r.h+=a.r.h;
            }else if(a.r.y==r.y&&a.r.h==r.h&&(a.r.x+a.r.w==r.x||r.x+r.w==a.r.x)){
                r.x=std::min(r.x,a.r.x);r.w+=a.r.w;
            }else continue;
            bool blocked=false;for(size_t k=j+1;k<out.size();++k){auto overlap=intersect(a.r,out[k].r);if(overlap.w&&overlap.h){blocked=true;break;}}
            if(blocked){
                // Codex: можно перенести позднюю часть назад, если она не перекрывает промежуточные слои.
                blocked=false;for(size_t k=j+1;k<out.size();++k){auto overlap=intersect(d.r,out[k].r);if(overlap.w&&overlap.h){blocked=true;break;}}
                if(blocked)continue;
                auto &earlier=out[j];earlier.sx+=(r.x-earlier.r.x)*earlier.step;earlier.sy+=r.y-earlier.r.y;earlier.r=r;merged_early=true;break;
            }
            d.sx+=(r.x-d.r.x)*d.step;d.sy+=r.y-d.r.y;d.r=r;
            for(auto &hint:backgrounds)if(hint.bitmap){if(hint.at==j)hint.bitmap=nullptr;else if(hint.at>j)--hint.at;}
            out.erase(out.begin()+j);j=out.size();checked=0;
        }
        if(!merged_early){
            if(d.opaque&&bitmap&&!bitmap->frame&&bitmap->single&&d.r.w*d.r.h>=4096){
                auto &hint=backgrounds[(reinterpret_cast<uintptr_t>(bitmap)>>4)&7];
                if(hint.bitmap!=bitmap||out[hint.at].r.w*out[hint.at].r.h<d.r.w*d.r.h)hint={bitmap,out.size()};
            }
            out.push_back(std::move(d));
        }
    }
    draws.swap(out);
}
