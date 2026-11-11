// SPDX-License-Identifier: GPL-3.0-only
// Authored byte-image controls. No game actors, sockets, device access or assertion-disabled test paths.
#include "state_delta.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <new>
#include <string>
#include <vector>

static unsigned checks = 0;
static void Require(bool yes, const char *why)
{ ++checks; if (!yes) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(2); } }
static CE_StateIdentity Id(uint32_t sequence, uint32_t epoch = 7)
{
    CE_StateIdentity id = {}; for (unsigned i = 0; i != 32; ++i) id.context[i] = uint8_t(i);
    id.epoch = epoch; id.sequence = sequence; return id;
}
static CE_StateImage Image(const std::vector<uint8_t> &v, uint32_t sequence)
{ return {Id(sequence), v.data(), uint32_t(v.size())}; }
static void Put32(std::vector<uint8_t> &wire, unsigned at, uint32_t n)
{ for (unsigned i = 0; i != 4; ++i) wire[at + i] = uint8_t(n >> (24 - 8 * i)); }
static std::vector<uint8_t> Encode(const CE_StateImage &base, const CE_StateImage &target)
{
    std::vector<uint8_t> wire(65536, 0x9d); uint32_t size = 0;
    Require(CE_StateDeltaEncode(&base, &target, wire.data(), uint32_t(wire.size()), &size) == CE_STATE_DELTA_OK, "encode succeeds");
    uint32_t required = 0;
    Require(CE_StateDeltaMeasure(&base, &target, &required) == 0 && required == size, "measure equals encode");
    wire.resize(size); return wire;
}
static CE_StateDeltaScratch scratch;
static void RoundTrip(const CE_StateImage &, const CE_StateImage &);
static void Aliases()
{
    struct alignas(CE_StateImage) Arena { uint8_t bytes[65536]; };
    Arena base_bytes = {}, target_bytes = {}, wire_bytes = {}, output = {}, work_bytes = {};
    std::memset(output.bytes, 0xa9, sizeof(output)); std::memset(work_bytes.bytes, 0x65, sizeof(work_bytes));
    target_bytes.bytes[0] = 1;
    CE_StateImage base = {Id(1), base_bytes.bytes, 256}, target = {Id(2), target_bytes.bytes, 256};
    const auto wire = Encode(base, target); std::memcpy(wire_bytes.bytes, wire.data(), wire.size());
    auto *work = new (work_bytes.bytes) CE_StateDeltaScratch;
    CE_StateIdentity identity = Id(99); uint32_t size = 0x12345678;
    struct Saved { const void *p; size_t n; std::vector<uint8_t> bytes; };
    unsigned refusals = 0;
    auto refused = [&](const char *why, auto call) {
        std::vector<Saved> saved;
        auto keep = [&](const void *p, size_t n) { const auto *b = static_cast<const uint8_t*>(p); saved.push_back({p, n, {b, b + n}}); };
        keep(&base, sizeof(base)); keep(&target, sizeof(target)); keep(&identity, sizeof(identity)); keep(&size, sizeof(size));
        for (auto *a : {&base_bytes, &target_bytes, &wire_bytes, &output, &work_bytes}) keep(a, sizeof(*a));
        Require(call() == CE_STATE_DELTA_INVALID, why);
        for (const auto &s : saved) Require(!std::memcmp(s.p, s.bytes.data(), s.n), "alias refusal preserves every owned object including scratch");
        ++refusals;
    };
    auto decode = [&](const CE_StateImage *b, CE_StateDeltaScratch *s, uint8_t *out, uint32_t capacity,
                      CE_StateIdentity *id, uint32_t *n, const uint8_t *w = nullptr) {
        return CE_StateDeltaDecode(b, 1, w ? w : wire_bytes.bytes, uint32_t(wire.size()), s, out, capacity, id, n);
    };
    refused("size cannot alias identity.epoch", [&] { return decode(&base, work, output.bytes, 65536, &identity, &identity.epoch); });
    refused("size cannot alias identity.sequence", [&] { return decode(&base, work, output.bytes, 65536, &identity, &identity.sequence); });
    refused("identity cannot alias input descriptor subobject", [&] { return decode(&base, work, output.bytes, 65536, &base.identity, &size); });
    refused("size cannot alias input descriptor field", [&] { return decode(&base, work, output.bytes, 65536, &identity, &base.size); });
    auto *wire_id = new (wire_bytes.bytes + 64) CE_StateIdentity;
    refused("identity cannot alias wire", [&] { return decode(&base, work, output.bytes, 65536, wire_id, &size); });
    auto *wire_size = new (wire_bytes.bytes) uint32_t;
    refused("size cannot alias wire", [&] { return decode(&base, work, output.bytes, 65536, &identity, wire_size); });
    auto *base_id = new (base_bytes.bytes + 64) CE_StateIdentity;
    refused("identity cannot alias basis data", [&] { return decode(&base, work, output.bytes, 65536, base_id, &size); });
    auto *base_size = new (base_bytes.bytes + 128) uint32_t;
    refused("size cannot alias basis data", [&] { return decode(&base, work, output.bytes, 65536, &identity, base_size); });
    auto *scratch_id = new (work_bytes.bytes + 64) CE_StateIdentity;
    refused("identity cannot alias scratch", [&] { return decode(&base, work, output.bytes, 65536, scratch_id, &size); });
    auto *scratch_size = new (work_bytes.bytes + 128) uint32_t;
    refused("size cannot alias scratch", [&] { return decode(&base, work, output.bytes, 65536, &identity, scratch_size); });
    auto *output_id = new (output.bytes + 512) CE_StateIdentity;
    refused("identity cannot alias output declared capacity beyond target", [&] { return decode(&base, work, output.bytes, 65536, output_id, &size); });
    auto *output_size = new (output.bytes + 1024) uint32_t;
    refused("size cannot alias output declared capacity beyond target", [&] { return decode(&base, work, output.bytes, 65536, &identity, output_size); });
    refused("output cannot alias scratch", [&] { return decode(&base, work, work_bytes.bytes, 65536, &identity, &size); });
    std::memcpy(work_bytes.bytes, wire.data(), wire.size());
    refused("scratch cannot alias wire", [&] { return decode(&base, work, output.bytes, 65536, &identity, &size, work_bytes.bytes); });
    auto *scratch_base = new (work_bytes.bytes + 64) CE_StateImage(base);
    refused("scratch cannot overwrite its input descriptor", [&] { return decode(scratch_base, work, output.bytes, 65536, &identity, &size); });
    auto *output_base = new (output.bytes + 64) CE_StateImage(base);
    refused("decode output cannot overwrite input descriptor", [&] { return decode(output_base, work, output.bytes, 65536, &identity, &size); });
    refused("digest output cannot overwrite input descriptor", [&] { return CE_StateImageDigest(&base, reinterpret_cast<uint8_t*>(&base)); });
    refused("measure scalar cannot overwrite basis descriptor", [&] { return CE_StateDeltaMeasure(&base, &target, &base.identity.epoch); });
    refused("measure scalar cannot overwrite target descriptor", [&] { return CE_StateDeltaMeasure(&base, &target, &target.size); });
    refused("measure scalar cannot overwrite basis data", [&] { return CE_StateDeltaMeasure(&base, &target, base_size); });
    auto *target_size = new (target_bytes.bytes + 128) uint32_t;
    refused("measure scalar cannot overwrite target data", [&] { return CE_StateDeltaMeasure(&base, &target, target_size); });
    refused("encode scalar cannot overwrite basis descriptor", [&] { return CE_StateDeltaEncode(&base, &target, output.bytes, 65536, &base.size); });
    refused("encode scalar cannot overwrite target descriptor", [&] { return CE_StateDeltaEncode(&base, &target, output.bytes, 65536, &target.identity.epoch); });
    refused("encode scalar cannot overwrite basis data", [&] { return CE_StateDeltaEncode(&base, &target, output.bytes, 65536, base_size); });
    refused("encode scalar cannot overwrite target data", [&] { return CE_StateDeltaEncode(&base, &target, output.bytes, 65536, target_size); });
    refused("encode scalar cannot alias output capacity", [&] { return CE_StateDeltaEncode(&base, &target, output.bytes, 65536, output_size); });
    refused("encode output cannot overwrite basis descriptor", [&] { return CE_StateDeltaEncode(output_base, &target, output.bytes, 65536, &size); });
    auto *output_target = new (output.bytes + 64) CE_StateImage(target);
    refused("encode output cannot overwrite target descriptor", [&] { return CE_StateDeltaEncode(&base, output_target, output.bytes, 65536, &size); });

    // Fabricated numerically overflowing ranges are rejected before dereference, without claiming address accessibility.
    const uintptr_t near_end = UINTPTR_MAX - 1;
    auto *bad_image = reinterpret_cast<const CE_StateImage*>(near_end);
    refused("descriptor address overflow refused before reads", [&] { return CE_StateImageDigest(bad_image, output.bytes); });
    refused("digest output address overflow refused", [&] { return CE_StateImageDigest(&base, reinterpret_cast<uint8_t*>(near_end)); });
    refused("measure scalar address overflow refused", [&] { return CE_StateDeltaMeasure(&base, &target, reinterpret_cast<uint32_t*>(near_end)); });
    refused("encode scalar address overflow refused", [&] { return CE_StateDeltaEncode(&base, &target, output.bytes, 65536, reinterpret_cast<uint32_t*>(near_end)); });
    refused("encode buffer address overflow refused", [&] { return CE_StateDeltaEncode(&base, &target, reinterpret_cast<uint8_t*>(near_end), 65536, &size); });
    refused("decode identity address overflow refused", [&] { return decode(&base, work, output.bytes, 65536, reinterpret_cast<CE_StateIdentity*>(near_end), &size); });
    refused("decode size address overflow refused", [&] { return decode(&base, work, output.bytes, 65536, &identity, reinterpret_cast<uint32_t*>(near_end)); });
    refused("decode output address overflow refused", [&] { return decode(&base, work, reinterpret_cast<uint8_t*>(near_end), 65536, &identity, &size); });
    refused("decode scratch address overflow refused", [&] { return decode(&base, reinterpret_cast<CE_StateDeltaScratch*>(near_end), output.bytes, 65536, &identity, &size); });
    refused("decode wire address overflow refused", [&] { return decode(&base, work, output.bytes, 65536, &identity, &size, reinterpret_cast<const uint8_t*>(near_end)); });
    CE_StateImage invalid_data = base; invalid_data.data = reinterpret_cast<const uint8_t*>(near_end);
    refused("basis data address overflow refused before reads", [&] { return decode(&invalid_data, work, output.bytes, 65536, &identity, &size); });
    refused("null nonempty encode output refused", [&] { return CE_StateDeltaEncode(&base, &target, nullptr, 65536, &size); });
    refused("null nonempty decode output refused", [&] { return decode(&base, work, nullptr, 65536, &identity, &size); });

    // Shared immutable data is legitimate. Null output/capacity0 is legitimate when the decoded target is empty.
    const std::vector<uint8_t> empty;
    const auto empty_wire = Encode(base, Image(empty, 2));
    Require(CE_StateDeltaDecode(&base, 1, empty_wire.data(), uint32_t(empty_wire.size()), work, nullptr, 0, &identity, &size) == 0 &&
            size == 0 && identity.sequence == 2 && identity.epoch == 7, "null zero-capacity empty target positive");
    CE_StateImage same_data = base; same_data.identity.sequence = 2;
    RoundTrip(base, same_data);
    Require(refusals == 41, "complete bounded alias/overflow refusal coverage");
    std::printf("{\"suite\":\"metadata-alias\",\"checks\":%u,\"refusals\":%u,\"failed\":0}\n", checks, refusals);
}
static void Refuse(const CE_StateImage &base, const std::vector<uint8_t> &wire, int expected = -1,
                   uint32_t floor = 1, uint32_t capacity = 65536)
{
    std::vector<uint8_t> out(65536, 0xa7), before = out;
    CE_StateIdentity id; std::memset(&id, 0x6b, sizeof(id)); const CE_StateIdentity old = id;
    uint32_t size = 0xdeadbeef;
    const int result = CE_StateDeltaDecode(&base, floor, wire.data(), uint32_t(wire.size()), &scratch,
                                          out.data(), capacity, &id, &size);
    Require(expected < 0 ? result != CE_STATE_DELTA_OK : result == expected, "refusal reason");
    Require(out == before && size == 0xdeadbeef && !std::memcmp(&id, &old, sizeof(id)), "refusal preserves output bytes and metadata");
}
static void RoundTrip(const CE_StateImage &base, const CE_StateImage &target)
{
    const auto wire = Encode(base, target);
    std::vector<uint8_t> out(65536, 0x42); CE_StateIdentity id = {}; uint32_t size = 0;
    Require(CE_StateDeltaDecode(&base, base.identity.sequence, wire.data(), uint32_t(wire.size()), &scratch,
                              out.data(), uint32_t(out.size()), &id, &size) == 0, "decode succeeds");
    Require(size == target.size && !std::memcmp(&id, &target.identity, sizeof(id)) &&
            (!size || !std::memcmp(out.data(), target.data, size)), "byte-exact roundtrip and target identity");
    Require(std::all_of(out.begin() + size, out.end(), [](uint8_t b) { return b == 0x42; }), "tail outside target unchanged");
}
static void Core()
{
    std::vector<uint8_t> base(4096, 0), target = base;
    target[0] = 1; target[127] = 2; target[128] = 3; target[4095] = 4;
    const auto b = Image(base, 1), t = Image(target, 2);
    const auto wire = Encode(b, t); RoundTrip(b, t);
    // Every prefix and every authenticated-header/payload bit alteration refuses without publishing a partial image.
    for (unsigned n = 0; n < wire.size(); ++n) Refuse(b, {wire.begin(), wire.begin() + n});
    for (unsigned i = 0; i < wire.size(); ++i) for (unsigned bit = 0; bit != 8; ++bit) {
        auto bad = wire; bad[i] ^= uint8_t(1u << bit); Refuse(b, bad);
    }
    auto extra = wire; extra.push_back(0); Refuse(b, extra);
    Refuse(b, wire, CE_STATE_DELTA_STALE, 2); Refuse(b, wire, CE_STATE_DELTA_STALE, 100);
    Refuse(b, wire, CE_STATE_DELTA_INVALID, 0); Refuse(b, wire, CE_STATE_DELTA_CAPACITY, 1, 4095);
    auto wrong = base; wrong[1000] = 1; const auto wrong_base = Image(wrong, 1);
    Refuse(wrong_base, wire, CE_STATE_DELTA_BASIS);
    auto id_base = b; ++id_base.identity.epoch; Refuse(id_base, wire, CE_STATE_DELTA_BASIS);
    id_base = b; ++id_base.identity.context[5]; Refuse(id_base, wire, CE_STATE_DELTA_BASIS);
    id_base = b; id_base.identity.sequence = 2; Refuse(id_base, wire, CE_STATE_DELTA_BASIS, 2);
    auto oversized = wire; oversized.resize(65537); Refuse(b, oversized, CE_STATE_DELTA_INVALID);

    // Valid target digests cannot make alternative/noncanonical run representations acceptable.
    std::vector<uint8_t> short_base(4, 0), short_target = {1, 2, 0, 0};
    const auto sb = Image(short_base, 1), st = Image(short_target, 2); const auto sw = Encode(sb, st);
    auto bad = sw; bad.resize(124); bad.insert(bad.end(), {0x80, 0, 2, 1, 2}); Refuse(sb, bad); // overlong skip
    bad = sw; bad.resize(124); bad.insert(bad.end(), {0, 0x82, 0, 1, 2}); Refuse(sb, bad); // overlong length
    bad = sw; bad.resize(124); bad.insert(bad.end(), {0, 1, 1, 0, 1, 2}); Refuse(sb, bad); // adjacent split
    bad = sw; bad.resize(124); bad.insert(bad.end(), {0, 3, 1, 2, 0}); Refuse(sb, bad); // unchanged literal
    bad = sw; bad.resize(124); bad.insert(bad.end(), {4, 0}); Refuse(sb, bad); // trailing zero-length run
    bad = sw; bad.resize(124); bad.insert(bad.end(), {0x80, 0x80, 0x80, 0, 2, 1, 2}); Refuse(sb, bad);
    bad = sw; bad.resize(124); bad.insert(bad.end(), {4, 1, 1}); Refuse(sb, bad); // past target
    bad = sw; Put32(bad, 56, 65537); Refuse(sb, bad);
    bad = sw; Put32(bad, 48, 0); Refuse(sb, bad);
    bad = sw; Put32(bad, 48, 1); Refuse(sb, bad);
    bad = sw; bad.resize(124); bad.insert(bad.end(), {0, 5, 1, 2}); Refuse(sb, bad);

    const std::vector<uint8_t> empty;
    RoundTrip(Image(empty, 1), Image(empty, 2));
    RoundTrip(Image(base, 1), Image(empty, 2));
    RoundTrip(Image(empty, 1), Image(base, 2)); // zero extension needs no literals
    RoundTrip(Image(base, 1), Image(base, 2));
    auto full = std::vector<uint8_t>(65536, 0); full.back() = 0xff;
    RoundTrip(Image(empty, 1), Image(full, 2));
    auto smaller = full; smaller.resize(16384); smaller.back() = 2;
    RoundTrip(Image(full, 1), Image(smaller, 2)); RoundTrip(Image(smaller, 1), Image(full, 2));
    const std::vector<uint8_t> exact_limit(65408, 1);
    Require(Encode(Image(empty, 1), Image(exact_limit, 2)).size() == 65536, "exact message bound is admissible");
    RoundTrip(Image(empty, 1), Image(exact_limit, 2));
    // No float interpretation: exact signed-zero, NaN payload, subnormal and arbitrary source-clock bytes survive.
    RoundTrip(Image({0,0,0,0, 0,0,0,1}, 1), Image({0x80,0,0,0, 0x7f,0xc0,0,0x19, 0,0,0,1}, 2));

    std::vector<uint8_t> noise(65536); for (unsigned i = 0; i != noise.size(); ++i) noise[i] = uint8_t(i % 255 + 1);
    auto nb = Image(base, 1), nt = Image(noise, 2); uint32_t needed = 0;
    Require(CE_StateDeltaMeasure(&nb, &nt, &needed) == 0 && needed > 65536, "oversize encoding measured honestly");
    std::vector<uint8_t> sentinel(65536, 0xa9); const auto original = sentinel; uint32_t written = 0xbeef;
    Require(CE_StateDeltaEncode(&nb, &nt, sentinel.data(), 65536, &written) == CE_STATE_DELTA_NOT_FIT &&
            sentinel == original && written == 0xbeef, "NOT_FIT preserves entire output");
    Require(CE_StateDeltaEncode(&b, &t, sentinel.data(), uint32_t(wire.size() - 1), &written) == CE_STATE_DELTA_CAPACITY &&
            sentinel == original && written == 0xbeef, "short encoder capacity preserves entire output");
    auto invalid = t; invalid.identity.sequence = 1; needed = 123;
    Require(CE_StateDeltaMeasure(&b, &invalid, &needed) == CE_STATE_DELTA_INVALID && needed == 123, "invalid pair measure atomic");
    invalid = t; ++invalid.identity.epoch;
    Require(CE_StateDeltaEncode(&b, &invalid, sentinel.data(), 65536, &written) == CE_STATE_DELTA_INVALID && sentinel == original, "different epoch encode refused");
    invalid = t; invalid.size = 65537;
    Require(CE_StateDeltaMeasure(&b, &invalid, &needed) == CE_STATE_DELTA_INVALID, "oversize image bound checked before reads");
    uint8_t digest[32]; std::memset(digest, 0x72, sizeof(digest));
    Require(CE_StateImageDigest(&invalid, digest) == CE_STATE_DELTA_INVALID && digest[0] == 0x72, "invalid digest output unchanged");
    auto same_address = target; const auto alias_before = same_address; auto alias_image = Image(same_address, 2);
    Require(CE_StateImageDigest(&alias_image, same_address.data()) == CE_STATE_DELTA_INVALID && same_address == alias_before,
            "digest cannot overwrite immutable image");
    Require(CE_StateDeltaEncode(&b, &alias_image, same_address.data(), uint32_t(same_address.size()), &written) == CE_STATE_DELTA_INVALID &&
            same_address == alias_before, "encoder alias refused");
    CE_StateIdentity oi = {}; uint32_t os = 0;
    Require(CE_StateDeltaDecode(&b, 1, wire.data(), uint32_t(wire.size()), &scratch, const_cast<uint8_t*>(b.data),
                              b.size, &oi, &os) == CE_STATE_DELTA_INVALID, "decode cannot overwrite pinned basis");
    Require(CE_StateDeltaDecode(&b, 1, wire.data(), uint32_t(wire.size()), &scratch, scratch.data,
                              65536, &oi, &os) == CE_STATE_DELTA_INVALID, "decode scratch/output alias refused");

    uint32_t random = 0x31415926;
    for (unsigned trial = 0; trial != 128; ++trial) {
        auto a = std::vector<uint8_t>(trial * 29), z = a;
        for (unsigned i = 0; i < a.size(); ++i) { random = random * 1664525 + 1013904223; a[i] = uint8_t(random >> 24); }
        z = a; if (trial % 3 == 0) z.resize(z.size() + 31, 0); else if (trial % 3 == 1 && z.size() > 9) z.resize(z.size() - 9);
        for (unsigned i = 0; i < z.size(); i += 31) z[i] ^= uint8_t(trial + 1);
        RoundTrip(Image(a, 1), Image(z, 2));
    }
    std::printf("{\"suite\":\"core\",\"checks\":%u,\"failed\":0}\n", checks);
}
static void Loss()
{
    // Exhaust every ordered subset of five independent targets anchored to the SAME modeled application-ACKED image.
    // Decode is not Apply. This model explicitly commits only successful Decode results; no actual World callback runs.
    const std::vector<uint8_t> base(100, 0); const auto b = Image(base, 1);
    std::vector<std::vector<uint8_t>> targets, wires;
    for (unsigned i = 0; i != 5; ++i) { auto t = base; t[i] = uint8_t(i + 1); targets.push_back(t); wires.push_back(Encode(b, Image(t, i + 2))); }
    unsigned schedules = 0;
    for (unsigned mask = 0; mask != 32; ++mask) {
        std::vector<unsigned> order; for (unsigned i = 0; i != 5; ++i) if (mask & (1u << i)) order.push_back(i);
        do {
            ++schedules; uint32_t floor = 1; std::vector<uint8_t> current = base;
            for (auto index : order) {
                const auto &wire = wires[index]; std::vector<uint8_t> out(100, 0xa5); const auto old = out;
                CE_StateIdentity id = {}; uint32_t size = 0;
                const int result = CE_StateDeltaDecode(&b, floor, wire.data(), uint32_t(wire.size()), &scratch,
                                                       out.data(), 100, &id, &size);
                if (index + 2 > floor) {
                    Require(result == 0 && size == 100 && out == targets[index], "loss/reorder exact newest reconstruction");
                    current = out; floor = id.sequence;
                } else Require(result == CE_STATE_DELTA_STALE && out == old && size == 0, "reorder refuses older target atomically");
                Refuse(b, wire, CE_STATE_DELTA_STALE, floor); // duplicate delivery
            }
            unsigned expected = 1; for (auto index : order) expected = std::max(expected, index + 2);
            Require(floor == expected && (expected == 1 ? current == base : current == targets[expected - 2]), "missing targets do not chain or advance basis");
        } while (std::next_permutation(order.begin(), order.end()));
    }
    Require(schedules == 326, "complete bounded ordered-subset schedule space");
    // A different, unacknowledged basis cannot decode against the pinned one. A lost ACK leaves old basis usable.
    const auto unacked = Image(targets[0], 2); auto later = targets[1]; later[20] = 8;
    const auto wrong_basis_wire = Encode(unacked, Image(later, 7));
    Refuse(b, wrong_basis_wire, CE_STATE_DELTA_BASIS);
    RoundTrip(b, Image(later, 7));
    // Explicit caller promotion after modeled Apply+exact digest ACK; old delayed packets cannot resurrect old basis.
    uint8_t acknowledged_digest[32], retained_digest[32];
    Require(CE_StateImageDigest(&unacked, acknowledged_digest) == 0 && CE_StateImageDigest(&unacked, retained_digest) == 0 &&
            !std::memcmp(acknowledged_digest, retained_digest, 32), "modeled exact ACK identifies retained candidate");
    RoundTrip(unacked, Image(later, 7));
    Refuse(unacked, wires[4], CE_STATE_DELTA_BASIS, 2);
    std::printf("{\"suite\":\"loss-reorder-model\",\"checks\":%u,\"schedules\":%u,\"failed\":0}\n", checks, schedules);
}
static std::vector<uint8_t> Read(const char *path)
{
    std::ifstream in(path, std::ios::binary); Require(bool(in), "open authored fixture");
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), {});
    Require(data.size() <= 65536, "fixture image within bound"); return data;
}
int main(int argc, char **argv)
{
    if (argc == 2 && !std::strcmp(argv[1], "aliases")) { Aliases(); return 0; }
    if (argc == 2 && !std::strcmp(argv[1], "core")) { Core(); return 0; }
    if (argc == 2 && !std::strcmp(argv[1], "loss")) { Loss(); return 0; }
    if (argc == 5 && !std::strcmp(argv[1], "encode")) {
        const auto base = Read(argv[2]), target = Read(argv[3]); const auto wire = Encode(Image(base, 1), Image(target, 2));
        RoundTrip(Image(base, 1), Image(target, 2));
        std::ofstream out(argv[4], std::ios::binary); out.write(reinterpret_cast<const char*>(wire.data()), std::streamsize(wire.size()));
        Require(bool(out), "write own encoded fixture"); std::printf("{\"size\":%zu,\"checks\":%u}\n", wire.size(), checks); return 0;
    }
    if (argc == 3 && !std::strcmp(argv[1], "fixtures")) {
        std::ifstream list(argv[2]); Require(bool(list), "open pair manifest"); std::string a, b; unsigned pairs = 0;
        while (std::getline(list, a) && std::getline(list, b)) {
            const auto base = Read(a.c_str()), target = Read(b.c_str());
            const auto bi = Image(base, 1), ti = Image(target, 2); uint32_t required = 0;
            Require(CE_StateDeltaMeasure(&bi, &ti, &required) == 0, "fixture measured");
            if (required <= 65536) RoundTrip(bi, ti);
            else { std::vector<uint8_t> out(65536, 0x31); uint32_t n = 9;
                Require(CE_StateDeltaEncode(&bi, &ti, out.data(), 65536, &n) == CE_STATE_DELTA_NOT_FIT && n == 9 && out[0] == 0x31,
                        "fixture not-fit explicit"); }
            std::printf("{\"pair\":%u,\"base\":%zu,\"target\":%zu,\"encoded\":%u}\n", pairs++, base.size(), target.size(), required);
        }
        std::printf("{\"suite\":\"authored-serializer\",\"pairs\":%u,\"checks\":%u,\"failed\":0}\n", pairs, checks); return pairs ? 0 : 2;
    }
    std::fprintf(stderr, "use aliases | core | loss | fixtures PAIRS | encode BASE TARGET OUTPUT\n"); return 2;
}
