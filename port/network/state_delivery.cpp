// SPDX-License-Identifier: GPL-3.0-only
#include "state_delivery.h"
#include <monocypher.h>
#include <cstring>
#include <new>
#include <cstddef>

static_assert(sizeof(CE_StateKey) == 80, "explicit key layout");
namespace {
bool Range(const void *p, size_t n) { return !n || (p && n <= UINTPTR_MAX - reinterpret_cast<uintptr_t>(p)); }
bool Overlap(const void *a, size_t an, const void *b, size_t bn) {
    return an && bn && (!Range(a, an) || !Range(b, bn) ||
           (reinterpret_cast<uintptr_t>(a) < reinterpret_cast<uintptr_t>(b) + bn &&
            reinterpret_cast<uintptr_t>(b) < reinterpret_cast<uintptr_t>(a) + an));
}
void Put(uint8_t *p, uint32_t x) { p[0]=uint8_t(x>>24); p[1]=uint8_t(x>>16); p[2]=uint8_t(x>>8); p[3]=uint8_t(x); }
uint32_t Get(const uint8_t *p) { return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3]; }
template<class T> bool Equal(const T &a,const T &b) { return std::memcmp(&a,&b,sizeof(T))==0; }
bool Zero(const void *p,size_t n) { const auto *b=static_cast<const uint8_t*>(p); for(size_t i=0;i<n;++i) if(b[i]) return false; return true; }
bool KeyValid(const CE_StateKey &k) { return k.epoch && k.sequence && k.tick && k.size<=CE_STATE_DELTA_MAX_IMAGE; }
bool GateValid(const CE_StateRepairGate &g) { return g.wait_generation && (g.lifecycle_baseline || !g.lifecycle_watermark); }
bool ControlValid(const CE_StateDeliveryControl *c) {
    if(!Range(c,sizeof(*c)) || c->kind<CE_DELIVERY_APPLIED || c->kind>CE_DELIVERY_REPAIR_READY || !KeyValid(c->key)) return false;
    const bool repair=c->kind==CE_DELIVERY_REPAIR || c->kind==CE_DELIVERY_REPAIR_READY;
    if(repair) return c->offer_generation && GateValid(c->gate) && KeyValid(c->active_base) &&
        c->active_base.epoch==c->key.epoch && crypto_verify32(c->active_base.context,c->key.context)==0 &&
        c->active_base.sequence<c->key.sequence && c->active_base.tick<=c->key.tick;
    if(!Zero(&c->gate,sizeof(c->gate)) || !Zero(&c->active_base,sizeof(c->active_base))) return false;
    return c->kind==CE_DELIVERY_MISSING_BASE ? !c->offer_generation : c->kind==CE_DELIVERY_APPLIED || c->offer_generation;
}
void WriteKey(uint8_t *p,const CE_StateKey &k) {
    std::memcpy(p,k.context,32); Put(p+32,k.epoch); Put(p+36,k.sequence); Put(p+40,k.tick); Put(p+44,k.size); std::memcpy(p+48,k.digest,32);
}
CE_StateKey ReadKey(const uint8_t *p) {
    CE_StateKey k={}; std::memcpy(k.context,p,32); k.epoch=Get(p+32); k.sequence=Get(p+36); k.tick=Get(p+40); k.size=Get(p+44); std::memcpy(k.digest,p+48,32); return k;
}
CE_StateKey Bind(const CE_StateIdentity &id,uint32_t tick,uint32_t size,const uint8_t image_digest[32]) {
    CE_StateKey k={}; std::memcpy(k.context,id.context,32); k.epoch=id.epoch; k.sequence=id.sequence; k.tick=tick; k.size=size;
    uint8_t numbers[16]; Put(numbers,k.epoch); Put(numbers+4,k.sequence); Put(numbers+8,tick); Put(numbers+12,size);
    static const uint8_t domain[]="CE-state-applied-v1";
    crypto_blake2b_ctx ctx; crypto_blake2b_init(&ctx,32); crypto_blake2b_update(&ctx,domain,sizeof(domain));
    crypto_blake2b_update(&ctx,k.context,32); crypto_blake2b_update(&ctx,numbers,sizeof(numbers));
    crypto_blake2b_update(&ctx,image_digest,32); crypto_blake2b_final(&ctx,k.digest); return k;
}
bool ImageOutside(const CE_StateImage *image,const void *p,size_t n) {
    return Range(image,sizeof(*image)) && !Overlap(p,n,image,sizeof(*image)) && !Overlap(p,n,image->data,image->size);
}
bool Scope(const CE_StateKey &key,const uint8_t context[32],uint32_t epoch) {
    return KeyValid(key) && key.epoch==epoch && crypto_verify32(key.context,context)==0;
}
bool Clock(uint64_t &last,uint64_t now) { if(now<last) return false; last=now; return true; }
CE_StateDeliveryControl Control(uint32_t kind,uint32_t generation,const CE_StateKey &key) {
    CE_StateDeliveryControl c={}; c.kind=kind; c.offer_generation=generation; c.key=key; return c;
}
struct Slot {
    bool valid=false; uint32_t offer_generation=0; CE_StateKey key={}; uint8_t bytes[CE_STATE_DELTA_MAX_IMAGE]={};
};
CE_StateImage View(const Slot &s) {
    CE_StateImage image={}; std::memcpy(image.identity.context,s.key.context,32); image.identity.epoch=s.key.epoch;
    image.identity.sequence=s.key.sequence; image.data=s.bytes; image.size=s.key.size; return image;
}
void Store(Slot &slot,const CE_StateImage &image,const CE_StateKey &key,uint32_t offer) {
    if(image.size) std::memmove(slot.bytes,image.data,image.size); slot.key=key; slot.offer_generation=offer; slot.valid=true;
}
bool TicketEqual(const CE_StateDeliveryTicket &a,const CE_StateDeliveryTicket &b) {
    return a.generation==b.generation && a.offer_generation==b.offer_generation && a.repair==b.repair && Equal(a.key,b.key);
}
bool Fresh(const CE_StateKey &k,const CE_StateKey &floor) { return k.sequence>floor.sequence && k.tick>=floor.tick; }
}

struct CE_StateSender {
    uint8_t context[32]={}; uint32_t epoch=0; Slot slots[2]; unsigned b=0,c=1;
    bool pending=false,baseline=false,repair_required=false,repair_waiting=false,prepared=false,announced=false,repair_ready=false;
    bool status_outstanding=false,had_offer=false,had_repair=false; uint32_t next_offer=0; uint64_t revision=1,serial=0,prepared_revision=0;
    uint64_t now=0,sent_at=0,status_at=0,last_offer_at=0,prepared_at=0,last_repair_at=0;
    CE_StateKey last_sent={}; CE_StateDeliveryTicket ticket={}; CE_StateDeliveryControl announcement={};
};
struct CE_StateReceiver {
    uint8_t context[32]={}; uint32_t epoch=0; Slot slots[2]; unsigned b=0,c=1;
    bool seeded=false,prepared=false,expect_repair=false,ack_pending=false,response_pending=false,missing_pending=false,missing_sent=false;
    uint32_t highest_offer=0,retired_offer=0; uint64_t now=0,serial=0,repair_at=0,missing_at=0;
    CE_StateKey applied={}; CE_StateDeliveryTicket ticket={}; CE_StateKey prepared_base={};
    CE_StateDeliveryControl expected={},ack={},response={},missing={};
    uint8_t bytes[CE_STATE_DELTA_MAX_IMAGE]={}; CE_StateDeltaScratch scratch={};
};

namespace {
template<class Owner,class Out> bool Output(const Owner *s,Out *out) {
    return Range(out,sizeof(*out)) && !Overlap(out,sizeof(*out),s,sizeof(*s));
}
bool ImageKey(const CE_StateImage *image,uint32_t tick,const uint8_t *context,uint32_t epoch,CE_StateKey &key) {
    return CE_StateDeliveryKey(image,tick,&key)==CE_DELIVERY_OK && Scope(key,context,epoch);
}
void Invalidate(CE_StateSender *s) {
    if(s->prepared && s->ticket.offer_generation && !s->pending) s->slots[s->c].valid=false;
    s->prepared=s->announced=s->repair_ready=false;
}
bool Revision(CE_StateSender *s) { if(s->revision==UINT64_MAX) return false; ++s->revision; Invalidate(s); return true; }
bool SenderTicket(CE_StateSender *s,const CE_StateDeliveryTicket *ticket) {
    return Range(ticket,sizeof(*ticket)) && s->prepared && TicketEqual(*ticket,s->ticket) && s->prepared_revision==s->revision;
}
bool ReceiverTicket(CE_StateReceiver *r,const CE_StateDeliveryTicket *ticket) {
    return Range(ticket,sizeof(*ticket)) && r->prepared && TicketEqual(*ticket,r->ticket);
}
void Promote(CE_StateSender *s) { s->b=s->c; s->c=1-s->b; s->slots[s->c].valid=false; s->pending=s->baseline=s->status_outstanding=false; }
bool PreparedOutputs(CE_StateSender *s,const CE_StateImage *image,uint8_t *out,uint32_t capacity,uint32_t *size,CE_StateDeliveryTicket *ticket) {
    return Range(out,capacity) && Output(s,size) && Output(s,ticket) && ImageOutside(image,out,capacity) &&
        ImageOutside(image,size,sizeof(*size)) && ImageOutside(image,ticket,sizeof(*ticket)) &&
        !Overlap(out,capacity,s,sizeof(*s)) && !Overlap(out,capacity,size,sizeof(*size)) &&
        !Overlap(out,capacity,ticket,sizeof(*ticket)) && !Overlap(size,sizeof(*size),ticket,sizeof(*ticket));
}
bool DeltaHeader(CE_StateReceiver *r,const uint8_t *wire,uint32_t n,CE_StateKey &base,CE_StateKey &target,uint32_t &offer) {
    if(n<CE_STATE_DELIVERY_ENVELOPE+CE_STATE_DELTA_HEADER || n>CE_STATE_DELTA_MAX_IMAGE ||
       std::memcmp(wire,"CSD1",4) || wire[4]!=0 || wire[5]!=1 || wire[6] || wire[7]>1) return false;
    const uint8_t *p=wire+20; if(std::memcmp(p,"CED1",4)||p[4]!=1||p[5]!=1||p[6]||p[7]) return false;
    CE_StateIdentity id={}; std::memcpy(id.context,p+8,32); id.epoch=Get(p+40); id.sequence=Get(p+44);
    base=Bind(id,Get(wire+12),Get(p+52),p+60); id.sequence=Get(p+48); target=Bind(id,Get(wire+8),Get(p+56),p+92);
    offer=Get(wire+16);
    return Scope(base,r->context,r->epoch) && Scope(target,r->context,r->epoch) && Fresh(target,base) &&
        ((wire[7]==1 && offer) || (!wire[7] && !offer));
}
bool RxOutput(CE_StateReceiver *r,const void *data,uint32_t n,CE_StateDeliveryTicket *ticket) {
    return Output(r,ticket) && !Overlap(ticket,sizeof(*ticket),data,n) && Range(data,n) && !Overlap(data,n,r,sizeof(*r));
}
int SenderControlAdmission(const CE_StateSender *s,const CE_StateDeliveryControl &c) {
    if(c.kind==CE_DELIVERY_STATUS||c.kind==CE_DELIVERY_REPAIR) return CE_DELIVERY_INVALID;
    if(c.kind==CE_DELIVERY_REPAIR_READY) {
        // An unannounced receipt remains stale; a receipt for the known announced transaction must be exact.
        if(!s->prepared||!s->ticket.repair||!s->announced||c.offer_generation!=s->ticket.offer_generation) return CE_DELIVERY_STALE;
        auto expected=s->announcement;expected.kind=CE_DELIVERY_REPAIR_READY;
        return Equal(c,expected)?CE_DELIVERY_OK:CE_DELIVERY_INVALID;
    }
    if(c.kind==CE_DELIVERY_MISSING_BASE) {
        if(!s->slots[s->b].valid) return CE_DELIVERY_NOT_READY;
        if(!Equal(c.key,s->slots[s->b].key)) return c.key.sequence<s->slots[s->b].key.sequence?CE_DELIVERY_STALE:CE_DELIVERY_INVALID;
        return !s->repair_required&&s->revision==UINT64_MAX?CE_DELIVERY_INVALID:CE_DELIVERY_OK;
    }
    // These replies can name only accepted sends. Unknown past identities need no unbounded history, but known
    // retained identities and impossible future proofs are still validated during terminal queue draining.
    if(c.offer_generation>s->next_offer||c.key.sequence>s->last_sent.sequence||c.key.tick>s->last_sent.tick) return CE_DELIVERY_INVALID;
    for(const auto &slot:s->slots) if(slot.valid&&(slot.offer_generation==c.offer_generation||slot.key.sequence==c.key.sequence)&&
        (slot.offer_generation!=c.offer_generation||!Equal(slot.key,c.key))) return CE_DELIVERY_INVALID;
    if(c.key.sequence==s->last_sent.sequence&&!Equal(c.key,s->last_sent)) return CE_DELIVERY_INVALID;
    if(!s->pending||c.offer_generation!=s->slots[s->c].offer_generation) return CE_DELIVERY_STALE;
    if(!Equal(c.key,s->slots[s->c].key)) return CE_DELIVERY_INVALID;
    if(c.kind==CE_DELIVERY_PENDING) return CE_DELIVERY_OK;
    if(c.kind==CE_DELIVERY_APPLIED) return s->revision==UINT64_MAX?CE_DELIVERY_INVALID:CE_DELIVERY_OK;
    if(c.kind==CE_DELIVERY_RETIRED||c.kind==CE_DELIVERY_SUPERSEDED)
        return s->baseline||s->revision==UINT64_MAX?CE_DELIVERY_INVALID:CE_DELIVERY_OK;
    return CE_DELIVERY_INVALID;
}
int ReceiverControlAdmission(const CE_StateReceiver *r,const CE_StateDeliveryControl &c,uint32_t &kind) {
    kind=0;
    for(const auto &slot:r->slots) if(slot.valid&&(slot.offer_generation==c.offer_generation||slot.key.sequence==c.key.sequence)&&
        (slot.offer_generation!=c.offer_generation||!Equal(slot.key,c.key))) return CE_DELIVERY_INVALID;
    if(r->prepared&&(r->ticket.offer_generation==c.offer_generation||r->ticket.key.sequence==c.key.sequence)&&
       (r->ticket.offer_generation!=c.offer_generation||!Equal(r->ticket.key,c.key))) return CE_DELIVERY_INVALID;
    if(r->expect_repair&&(r->expected.offer_generation==c.offer_generation||r->expected.key.sequence==c.key.sequence)&&
       (r->expected.offer_generation!=c.offer_generation||!Equal(r->expected.key,c.key))) return CE_DELIVERY_INVALID;
    if(c.key.sequence==r->applied.sequence&&!Equal(c.key,r->applied)) return CE_DELIVERY_INVALID;
    for(const auto &slot:r->slots) if(slot.valid&&slot.offer_generation==c.offer_generation&&Equal(slot.key,c.key)) kind=CE_DELIVERY_APPLIED;
    if(!kind&&r->prepared&&r->ticket.offer_generation==c.offer_generation&&Equal(r->ticket.key,c.key)) kind=CE_DELIVERY_PENDING;
    if(!kind&&r->expect_repair&&r->expected.offer_generation==c.offer_generation&&Equal(r->expected.key,c.key)) kind=CE_DELIVERY_PENDING;
    if(!kind) {
        if((r->prepared&&r->ticket.offer_generation&&c.offer_generation>r->ticket.offer_generation)||
           (r->expect_repair&&c.offer_generation>r->expected.offer_generation)) return CE_DELIVERY_INVALID;
        kind=c.offer_generation<r->highest_offer||c.key.sequence<=r->applied.sequence?CE_DELIVERY_SUPERSEDED:CE_DELIVERY_RETIRED;
    }
    return CE_DELIVERY_OK;
}
struct RepairAdmission { int result; bool clock_reached; };
RepairAdmission ReceiverRepairAdmission(const CE_StateReceiver *r,const CE_StateDeliveryControl *c,
                                        const CE_StateRepairGate *gate,uint64_t now) {
    if(!r||!ControlValid(c)||c->kind!=CE_DELIVERY_REPAIR||!Range(gate,sizeof(*gate))||!Equal(c->gate,*gate)||!GateValid(*gate)||
       !Scope(c->key,r->context,r->epoch)||now<r->now) return {CE_DELIVERY_INVALID,false};
    // ExpectRepair historically reaches Clock before these phase/identity checks, even when they refuse.
    // Report that boundary without changing it, so the read-only check cannot advance the owner's clock.
    if(!r->seeded) return {CE_DELIVERY_INVALID,true};
    for(const auto &slot:r->slots) if(slot.valid&&slot.key.sequence==c->active_base.sequence&&!Equal(slot.key,c->active_base)) return {CE_DELIVERY_INVALID,true};
    if(r->expect_repair) {
        if(!Equal(r->expected,*c)) return {CE_DELIVERY_INVALID,true};
    } else {
        if(!Fresh(c->key,r->applied)||(r->prepared&&c->key.sequence<=r->ticket.key.sequence)||c->offer_generation<=r->retired_offer) return {CE_DELIVERY_STALE,true};
        if(c->offer_generation<=r->highest_offer) return {CE_DELIVERY_INVALID,true};
        if(r->prepared&&c->key.tick<r->ticket.key.tick) return {CE_DELIVERY_INVALID,true};
    }
    return {CE_DELIVERY_OK,true};
}
}

int CE_StateDeliveryKey(const CE_StateImage *image,uint32_t tick,CE_StateKey *out) {
    if(!tick || !Range(out,sizeof(*out)) || !ImageOutside(image,out,sizeof(*out))) return CE_DELIVERY_INVALID;
    uint8_t digest[32]; if(CE_StateImageDigest(image,digest)!=CE_STATE_DELTA_OK) return CE_DELIVERY_INVALID;
    const auto key=Bind(image->identity,tick,image->size,digest); *out=key; return CE_DELIVERY_OK;
}
int CE_StateDeliveryEncodeControl(const CE_StateDeliveryControl *c,uint8_t *out,uint32_t capacity) {
    if(!ControlValid(c) || !Range(out,capacity) || Overlap(out,capacity,c,sizeof(*c))) return CE_DELIVERY_INVALID;
    if(capacity<CE_STATE_DELIVERY_CONTROL_BYTES) return CE_DELIVERY_NOT_FIT;
    uint8_t p[184]={}; std::memcpy(p,"CDA1",4); p[4]=1; p[5]=uint8_t(c->kind); Put(p+8,c->offer_generation);
    Put(p+12,c->gate.wait_generation); Put(p+16,c->gate.lifecycle_baseline); Put(p+20,c->gate.lifecycle_watermark);
    WriteKey(p+24,c->key); WriteKey(p+104,c->active_base); std::memcpy(out,p,sizeof(p)); return CE_DELIVERY_OK;
}
int CE_StateDeliveryDecodeControl(const uint8_t *p,uint32_t n,CE_StateDeliveryControl *out) {
    if(n!=184 || !Range(p,n) || !Range(out,sizeof(*out)) || Overlap(p,n,out,sizeof(*out)) ||
       std::memcmp(p,"CDA1",4) || p[4]!=1 || p[6] || p[7]) return CE_DELIVERY_INVALID;
    CE_StateDeliveryControl c={}; c.kind=p[5]; c.offer_generation=Get(p+8); c.gate={Get(p+12),Get(p+16),Get(p+20)};
    c.key=ReadKey(p+24); c.active_base=ReadKey(p+104); if(!ControlValid(&c)) return CE_DELIVERY_INVALID;
    *out=c; return CE_DELIVERY_OK;
}
CE_StateSender *CE_StateSenderCreate(const uint8_t context[32],uint32_t epoch) {
    if(!Range(context,32)||!epoch) return nullptr; auto *s=new(std::nothrow) CE_StateSender;
    if(s) {std::memcpy(s->context,context,32);s->epoch=epoch;} return s;
}
CE_StateReceiver *CE_StateReceiverCreate(const uint8_t context[32],uint32_t epoch) {
    if(!Range(context,32)||!epoch) return nullptr; auto *r=new(std::nothrow) CE_StateReceiver;
    if(r) {std::memcpy(r->context,context,32);r->epoch=epoch;} return r;
}
void CE_StateSenderDestroy(CE_StateSender *s) { delete s; }
void CE_StateReceiverDestroy(CE_StateReceiver *r) { delete r; }
int CE_StateSenderBaselineSent(CE_StateSender *s,const CE_StateImage *image,uint32_t tick,int sent,uint64_t now) {
    if(!s||!Clock(s->now,now)||s->slots[s->b].valid||s->pending||s->prepared||sent<0||sent>1) return CE_DELIVERY_INVALID;
    CE_StateKey key={}; if(!ImageKey(image,tick,s->context,s->epoch,key)) return CE_DELIVERY_INVALID;
    if(!sent) return CE_DELIVERY_RETRY; Store(s->slots[s->c],*image,key,0); s->pending=s->baseline=true; s->sent_at=now; s->last_sent=key; return CE_DELIVERY_OK;
}
int CE_StateReceiverBaselineApplied(CE_StateReceiver *r,const CE_StateImage *image,uint32_t tick,uint64_t now) {
    if(!r||!Clock(r->now,now)||r->seeded) return CE_DELIVERY_INVALID;
    CE_StateKey key={}; if(!ImageKey(image,tick,r->context,r->epoch,key)) return CE_DELIVERY_INVALID;
    Store(r->slots[r->b],*image,key,0); r->seeded=true; r->applied=key; r->ack=Control(CE_DELIVERY_APPLIED,0,key); r->ack_pending=true; return CE_DELIVERY_OK;
}
int CE_StateSenderBasis(const CE_StateSender *s,CE_StateKey *out) {
    if(!s||!Output(s,out)) return CE_DELIVERY_INVALID; if(!s->slots[s->b].valid) return CE_DELIVERY_NOT_READY;
    *out=s->slots[s->b].key; return CE_DELIVERY_OK;
}
int CE_StateSenderPrepare(CE_StateSender *s,const CE_StateImage *image,uint32_t tick,uint64_t now,
                          uint8_t *out,uint32_t capacity,uint32_t *size,CE_StateDeliveryTicket *ticket) {
    if(!s||!Clock(s->now,now)||!PreparedOutputs(s,image,out,capacity,size,ticket)||s->prepared) return CE_DELIVERY_INVALID;
    if(!s->slots[s->b].valid) return CE_DELIVERY_NOT_READY;
    if(s->repair_required||s->repair_waiting) return CE_DELIVERY_REPAIR_REQUIRED;
    CE_StateKey key={}; if(!ImageKey(image,tick,s->context,s->epoch,key)) return CE_DELIVERY_INVALID;
    if(!Fresh(key,s->last_sent)) return CE_DELIVERY_STALE;
    const bool offer=!s->pending && (!s->had_offer || now-s->last_offer_at>=CE_STATE_DELIVERY_OFFER_MS);
    if(s->serial==UINT64_MAX || (offer && s->next_offer==UINT32_MAX)) return CE_DELIVERY_INVALID;
    if(capacity<20) return CE_DELIVERY_NOT_FIT;
    const auto base=View(s->slots[s->b]); uint32_t required=0;
    if(CE_StateDeltaMeasure(&base,image,&required)!=0) return CE_DELIVERY_INVALID;
    if(required>65536-20) { s->repair_required=true; return CE_DELIVERY_REPAIR_REQUIRED; }
    if(required>capacity-20) return CE_DELIVERY_NOT_FIT;
    uint32_t n=0; if(CE_StateDeltaEncode(&base,image,out+20,capacity-20,&n)!=0) return CE_DELIVERY_INVALID;
    std::memcpy(out,"CSD1",4); out[4]=0;out[5]=1;out[6]=0;out[7]=offer?1:0;
    Put(out+8,tick);Put(out+12,base.identity.sequence?s->slots[s->b].key.tick:0);Put(out+16,offer?s->next_offer+1:0);
    s->ticket={++s->serial,offer?++s->next_offer:0,0,key}; s->prepared=true;s->prepared_revision=s->revision;s->prepared_at=now;
    if(offer) Store(s->slots[s->c],*image,key,s->ticket.offer_generation);
    *size=n+20;*ticket=s->ticket;return CE_DELIVERY_OK;
}
int CE_StateSenderValidate(CE_StateSender *s,const CE_StateDeliveryTicket *ticket,const CE_StateImage *image,uint32_t tick) {
    if(!s||!SenderTicket(s,ticket)) return CE_DELIVERY_STALE;
    CE_StateKey key={}; if(!ImageKey(image,tick,s->context,s->epoch,key)||!Equal(key,ticket->key)) return CE_DELIVERY_INVALID;
    if(ticket->repair && (!s->announced||!s->repair_ready)) return CE_DELIVERY_NOT_READY;
    return CE_DELIVERY_OK;
}
int CE_StateSenderSent(CE_StateSender *s,const CE_StateDeliveryTicket *ticket,const CE_StateImage *image,uint32_t tick,int sent,uint64_t now) {
    if(!s||!Clock(s->now,now)||sent<0||sent>1) return CE_DELIVERY_INVALID;
    int valid=CE_StateSenderValidate(s,ticket,image,tick); if(valid!=0) return valid;
    if(!sent) {if(!ticket->repair) Invalidate(s);return CE_DELIVERY_RETRY;}
    s->last_sent=ticket->key;
    if(ticket->offer_generation) {s->pending=true;s->baseline=false;s->had_offer=true;s->sent_at=now;s->last_offer_at=now;s->status_at=now;s->status_outstanding=false;}
    if(ticket->repair) {s->repair_waiting=s->had_repair=true;s->last_repair_at=now;}
    s->prepared=false;return CE_DELIVERY_OK;
}
int CE_StateSenderControl(CE_StateSender *s,const CE_StateDeliveryControl *c,uint64_t now,int terminal) {
    if(!s||!ControlValid(c)||!Scope(c->key,s->context,s->epoch)||now<s->now) return CE_DELIVERY_INVALID;
    if(!terminal) s->now=now;
    const int admission=SenderControlAdmission(s,*c);if(admission!=CE_DELIVERY_OK) return admission;
    if(terminal) return CE_DELIVERY_STALE;
    if(c->kind==CE_DELIVERY_REPAIR_READY) {
        s->repair_ready=true;return CE_DELIVERY_OK;
    }
    if(c->kind==CE_DELIVERY_MISSING_BASE) {
        if(!s->repair_required) {if(!Revision(s)) return CE_DELIVERY_INVALID;s->repair_required=true;} return CE_DELIVERY_REPAIR_REQUIRED;
    }
    if(c->kind==CE_DELIVERY_PENDING) {s->status_outstanding=false;s->status_at=now;return CE_DELIVERY_OK;}
    if(c->kind==CE_DELIVERY_APPLIED) {
        if(!Revision(s)) return CE_DELIVERY_INVALID; const bool repaired=s->repair_waiting;Promote(s);
        if(repaired) s->repair_required=s->repair_waiting=false;return CE_DELIVERY_OK;
    }
    if(c->kind==CE_DELIVERY_RETIRED||c->kind==CE_DELIVERY_SUPERSEDED) {
        if(s->baseline) return CE_DELIVERY_INVALID;
        if(!Revision(s)) return CE_DELIVERY_INVALID;s->slots[s->c].valid=false;s->pending=s->status_outstanding=s->repair_waiting=false;return CE_DELIVERY_OK;
    }
    return CE_DELIVERY_INVALID;
}
int CE_StateSenderPoll(CE_StateSender *s,uint64_t now,CE_StateDeliveryControl *out) {
    if(!s||!Output(s,out)||!Clock(s->now,now)) return CE_DELIVERY_INVALID;
    if((s->pending&&now-s->sent_at>=30000)||(s->prepared&&s->ticket.repair&&now-s->prepared_at>=30000)) return CE_DELIVERY_TIMEOUT;
    if(s->prepared&&s->ticket.repair&&!s->announced) {*out=s->announcement;return CE_DELIVERY_OK;}
    if(!s->pending||s->baseline||s->status_outstanding||now-s->status_at<CE_STATE_DELIVERY_STATUS_MS) return CE_DELIVERY_RETRY;
    *out=Control(CE_DELIVERY_STATUS,s->slots[s->c].offer_generation,s->slots[s->c].key);return CE_DELIVERY_OK;
}
int CE_StateSenderControlQueued(CE_StateSender *s,const CE_StateDeliveryControl *c,int sent,uint64_t now) {
    if(!s||!ControlValid(c)||!Clock(s->now,now)||sent<0||sent>1) return CE_DELIVERY_INVALID;
    CE_StateDeliveryControl pending={}; int result=CE_StateSenderPoll(s,now,&pending);
    if(result!=0||!Equal(*c,pending)) return CE_DELIVERY_STALE;if(!sent) return CE_DELIVERY_RETRY;
    if(c->kind==CE_DELIVERY_REPAIR) s->announced=true;else {s->status_outstanding=true;s->status_at=now;}return CE_DELIVERY_OK;
}
int CE_StateSenderPrepareRepair(CE_StateSender *s,const CE_StateImage *image,uint32_t tick,const CE_StateRepairGate *gate,
                                uint64_t now,CE_StateDeliveryControl *announcement,CE_StateDeliveryTicket *ticket) {
    if(!s||!Range(gate,sizeof(*gate))||!GateValid(*gate)||!Output(s,announcement)||!Output(s,ticket)||
       Overlap(announcement,sizeof(*announcement),ticket,sizeof(*ticket))||!ImageOutside(image,announcement,sizeof(*announcement))||
       !ImageOutside(image,ticket,sizeof(*ticket))||Overlap(announcement,sizeof(*announcement),gate,sizeof(*gate))||
       Overlap(ticket,sizeof(*ticket),gate,sizeof(*gate))||!Clock(s->now,now)) return CE_DELIVERY_INVALID;
    if(!s->repair_required||!s->slots[s->b].valid||s->pending||s->prepared) return CE_DELIVERY_NOT_READY;
    if(s->had_repair&&now-s->last_repair_at<CE_STATE_DELIVERY_REPAIR_INTERVAL_MS) return CE_DELIVERY_RETRY;
    CE_StateKey key={};if(!ImageKey(image,tick,s->context,s->epoch,key)) return CE_DELIVERY_INVALID;
    if(!Fresh(key,s->last_sent)) return CE_DELIVERY_STALE;
    if(s->serial==UINT64_MAX||s->next_offer==UINT32_MAX) return CE_DELIVERY_INVALID;
    s->ticket={++s->serial,++s->next_offer,1,key};s->prepared_revision=s->revision;s->prepared=s->repair_required=true;
    s->announced=s->repair_ready=false;s->prepared_at=now;Store(s->slots[s->c],*image,key,s->ticket.offer_generation);
    s->announcement=Control(CE_DELIVERY_REPAIR,s->ticket.offer_generation,key);s->announcement.active_base=s->slots[s->b].key;s->announcement.gate=*gate;
    *announcement=s->announcement;*ticket=s->ticket;return CE_DELIVERY_OK;
}
int CE_StateSenderRepairImage(CE_StateSender *s,const CE_StateDeliveryTicket *ticket,CE_StateImage *out) {
    if(!s||!Output(s,out)||!Range(ticket,sizeof(*ticket))||Overlap(out,sizeof(*out),ticket,sizeof(*ticket))) return CE_DELIVERY_INVALID;
    if(!SenderTicket(s,ticket)||!ticket->repair) return CE_DELIVERY_STALE;
    if(!s->announced||!s->repair_ready) return CE_DELIVERY_NOT_READY;*out=View(s->slots[s->c]);return CE_DELIVERY_OK;
}

int CE_StateReceiverPrepare(CE_StateReceiver *r,const uint8_t *wire,uint32_t n,uint64_t now,CE_StateDeliveryTicket *out) {
    if(!r||!RxOutput(r,wire,n,out)||!Clock(r->now,now)) return CE_DELIVERY_INVALID;
    if(!r->seeded) return CE_DELIVERY_NOT_READY;
    CE_StateKey base={},target={};uint32_t offer=0;if(!DeltaHeader(r,wire,n,base,target,offer)) return CE_DELIVERY_INVALID;
    if(!Fresh(target,r->applied)||(r->prepared&&target.sequence<=r->ticket.key.sequence)||(offer&&offer<=r->retired_offer)) return CE_DELIVERY_STALE;
    if(offer&&offer<=r->highest_offer) return CE_DELIVERY_INVALID;
    if(r->prepared&&target.tick<r->ticket.key.tick) return CE_DELIVERY_INVALID;
    if(r->expect_repair) return CE_DELIVERY_RETRY;
    Slot *basis=nullptr;for(auto &slot:r->slots) if(slot.valid&&Equal(slot.key,base)) basis=&slot;
    if(!basis) {
        r->missing=Control(CE_DELIVERY_MISSING_BASE,0,base);r->missing_pending=true;return CE_DELIVERY_REPAIR_REQUIRED;
    }
    if(offer&&r->slots[r->c].valid&&basis!=&r->slots[r->c]) return CE_DELIVERY_INVALID; // never evict an unconfirmed applied offer
    if(r->serial==UINT64_MAX) return CE_DELIVERY_INVALID;
    const auto image=View(*basis);CE_StateIdentity id={};uint32_t size=0;
    int result=CE_StateDeltaDecode(&image,r->applied.sequence,wire+20,n-20,&r->scratch,r->bytes,65536,&id,&size);
    if(result!=0) return CE_DELIVERY_INVALID;
    r->ticket={++r->serial,offer,0,target};r->prepared_base=base;r->prepared=true;
    if(offer>r->highest_offer) r->highest_offer=offer;*out=r->ticket;return CE_DELIVERY_OK;
}
int CE_StateReceiverCheckRepair(const CE_StateReceiver *r,const CE_StateDeliveryControl *c,const CE_StateRepairGate *gate,uint64_t now) {
    return ReceiverRepairAdmission(r,c,gate,now).result;
}
int CE_StateReceiverExpectRepair(CE_StateReceiver *r,const CE_StateDeliveryControl *c,const CE_StateRepairGate *gate,uint64_t now) {
    const auto admission=ReceiverRepairAdmission(r,c,gate,now);
    if(admission.clock_reached) r->now=now;
    if(admission.result!=CE_DELIVERY_OK) return admission.result;
    if(!r->expect_repair) {
        r->expected=*c;r->expect_repair=true;r->repair_at=now;r->highest_offer=c->offer_generation;
    }
    r->response=*c;r->response.kind=CE_DELIVERY_REPAIR_READY;r->response_pending=true;return CE_DELIVERY_OK;
}
int CE_StateReceiverPrepareRepair(CE_StateReceiver *r,const uint8_t *raw,uint32_t n,uint64_t now,CE_StateDeliveryTicket *out) {
    if(!r||n>65536||!RxOutput(r,raw,n,out)||!Clock(r->now,now)) return CE_DELIVERY_INVALID;
    if(!r->expect_repair) return CE_DELIVERY_NOT_READY;
    if(now-r->repair_at>=30000) return CE_DELIVERY_TIMEOUT;
    if(n!=r->expected.key.size||!Fresh(r->expected.key,r->applied)||(r->prepared&&r->expected.key.sequence<=r->ticket.key.sequence)) return CE_DELIVERY_STALE;
    CE_StateImage image={};std::memcpy(image.identity.context,r->context,32);image.identity.epoch=r->epoch;
    image.identity.sequence=r->expected.key.sequence;image.data=raw;image.size=n;CE_StateKey key={};
    if(!ImageKey(&image,r->expected.key.tick,r->context,r->epoch,key)||!Equal(key,r->expected.key)||r->serial==UINT64_MAX) return CE_DELIVERY_INVALID;
    if(n) std::memcpy(r->bytes,raw,n);r->ticket={++r->serial,r->expected.offer_generation,1,key};
    r->prepared_base=r->expected.active_base;r->prepared=true;*out=r->ticket;return CE_DELIVERY_OK;
}
int CE_StateReceiverImage(CE_StateReceiver *r,const CE_StateDeliveryTicket *ticket,CE_StateImage *out) {
    if(!r||!Output(r,out)||!Range(ticket,sizeof(*ticket))||Overlap(out,sizeof(*out),ticket,sizeof(*ticket))) return CE_DELIVERY_INVALID;
    if(!ReceiverTicket(r,ticket)) return CE_DELIVERY_STALE;
    CE_StateImage image={};std::memcpy(image.identity.context,r->ticket.key.context,32);image.identity.epoch=r->ticket.key.epoch;
    image.identity.sequence=r->ticket.key.sequence;image.data=r->bytes;image.size=r->ticket.key.size;*out=image;return CE_DELIVERY_OK;
}
int CE_StateReceiverApplied(CE_StateReceiver *r,const CE_StateDeliveryTicket *ticket,uint32_t epoch,uint32_t sequence,uint32_t tick,uint64_t now) {
    if(!r||!Clock(r->now,now)||!ReceiverTicket(r,ticket)) return CE_DELIVERY_STALE;
    if(epoch!=ticket->key.epoch||sequence!=ticket->key.sequence||tick!=ticket->key.tick||!Fresh(ticket->key,r->applied)) return CE_DELIVERY_INVALID;
    const bool used_candidate=r->slots[r->c].valid&&Equal(r->prepared_base,r->slots[r->c].key);
    if(ticket->offer_generation&&r->slots[r->c].valid&&!used_candidate&&!ticket->repair) return CE_DELIVERY_INVALID;
    if(used_candidate) {r->b=r->c;r->c=1-r->b;r->slots[r->c].valid=false;}
    if(ticket->offer_generation) {
        CE_StateImage image={};std::memcpy(image.identity.context,r->context,32);image.identity.epoch=r->epoch;
        image.identity.sequence=sequence;image.data=r->bytes;image.size=ticket->key.size;
        Store(r->slots[r->c],image,ticket->key,ticket->offer_generation);
        r->ack=Control(CE_DELIVERY_APPLIED,ticket->offer_generation,ticket->key);r->ack_pending=true;
    }
    r->applied=ticket->key;r->prepared=false;
    if(ticket->repair) r->expect_repair=false;
    return CE_DELIVERY_OK;
}
int CE_StateReceiverDiscard(CE_StateReceiver *r,const CE_StateDeliveryTicket *ticket) {
    if(!r||!ReceiverTicket(r,ticket)) return CE_DELIVERY_STALE;r->prepared=false;return CE_DELIVERY_OK;
}
int CE_StateReceiverControl(CE_StateReceiver *r,const CE_StateDeliveryControl *c,uint64_t now,int terminal) {
    if(!r||!ControlValid(c)||c->kind!=CE_DELIVERY_STATUS||!Scope(c->key,r->context,r->epoch)||now<r->now) return CE_DELIVERY_INVALID;
    if(!terminal) r->now=now;
    uint32_t kind=0;const int admission=ReceiverControlAdmission(r,*c,kind);if(admission!=CE_DELIVERY_OK) return admission;
    if(terminal) return CE_DELIVERY_STALE;
    if(kind==CE_DELIVERY_SUPERSEDED||kind==CE_DELIVERY_RETIRED) {
        if(c->offer_generation>r->retired_offer) r->retired_offer=c->offer_generation;
    }
    if(c->offer_generation>r->highest_offer) r->highest_offer=c->offer_generation;
    r->response=Control(kind,c->offer_generation,c->key);r->response_pending=true;return CE_DELIVERY_OK;
}
int CE_StateReceiverPoll(CE_StateReceiver *r,uint64_t now,CE_StateDeliveryControl *out) {
    if(!r||!Output(r,out)||!Clock(r->now,now)) return CE_DELIVERY_INVALID;
    if(r->expect_repair&&now-r->repair_at>=30000) return CE_DELIVERY_TIMEOUT;
    if(r->ack_pending) {*out=r->ack;return CE_DELIVERY_OK;}
    if(r->response_pending) {*out=r->response;return CE_DELIVERY_OK;}
    if(r->missing_pending&&(!r->missing_sent||now-r->missing_at>=1000)) {*out=r->missing;return CE_DELIVERY_OK;}return CE_DELIVERY_RETRY;
}
int CE_StateReceiverControlQueued(CE_StateReceiver *r,const CE_StateDeliveryControl *c,int sent,uint64_t now) {
    if(!r||!ControlValid(c)||!Clock(r->now,now)||sent<0||sent>1) return CE_DELIVERY_INVALID;
    CE_StateDeliveryControl pending={};int result=CE_StateReceiverPoll(r,now,&pending);
    if(result!=0||!Equal(*c,pending)) return CE_DELIVERY_STALE;if(!sent) return CE_DELIVERY_RETRY;
    if(r->ack_pending) r->ack_pending=false;else if(r->response_pending) r->response_pending=false;
    else {r->missing_pending=false;r->missing_at=now;r->missing_sent=true;}return CE_DELIVERY_OK;
}
