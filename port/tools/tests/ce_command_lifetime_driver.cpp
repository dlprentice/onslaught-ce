#include "ce_command_lifetime.h"
#include <cstdio>
#include <limits>
#include <type_traits>
static_assert(std::is_trivially_destructible<CeCommandLifetimeTable<4>>::value,
    "late native callbacks cannot depend on a table destructor");
static_assert(!std::is_copy_constructible<CeCommandLifetimeTable<4>>::value,
    "a second table must not clone the same identity sequence");
static CeCommandLifetimeTable<4> staticTable;
int checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"lifetime check failed at line %d\n",__LINE__); return 1; } } while(0)
int main() {
    CeCommandLifetimeTable<4> table;
    CeCommandLifetimeKey a{},b{},replacement{};
    CeCommandLifetimeRef ref{};
    CHECK(!table.Observe(0,0,1,&a) && a.world==0 && a.generation==0);
    CHECK(table.Observe(100,0,1,&a));
    CHECK(table.Resolve(a,&ref) && ref.address==100 && ref.id==0);
    CHECK(table.Observe(100,0,1,&b));
    CHECK(a.world==b.world && a.id==b.id && a.generation==b.generation);
    CHECK(table.Size()==1);
    CHECK(table.Observe(200,0,1,&b));
    CHECK(b.generation!=a.generation);
    CHECK(table.Resolve(a,&ref) && ref.address==100);
    CHECK(table.Resolve(b,&ref) && ref.address==200);
    CHECK(table.Forget(100));
    CHECK(!table.Resolve(a,&ref) && ref.address==0);
    CHECK(table.Resolve(b,&ref) && ref.address==200);
    CHECK(!table.Forget(100));
    CHECK(table.Observe(100,0,1,&replacement));
    CHECK(replacement.generation!=a.generation && replacement.world==a.world);
    CHECK(!table.Resolve(a,&ref));
    CHECK(table.Resolve(replacement,&ref) && ref.address==100);
    a=replacement;
    CHECK(table.Observe(100,9,1,&replacement));
    CHECK(replacement.generation!=a.generation && !table.Resolve(a,&ref));
    a=replacement;
    CHECK(table.Observe(100,9,2,&replacement));
    CHECK(replacement.generation!=a.generation && !table.Resolve(a,&ref));
    CHECK(table.Resolve(replacement,&ref) && ref.kind==2);
    CHECK(!table.Forget(999) && table.Size()==2);
    a=replacement;
    table.EndWorld();
    CHECK(table.Size()==0 && table.World()!=a.world);
    CHECK(!table.Resolve(a,&ref) && !table.Resolve(b,&ref));
    CHECK(!table.Forget(100));
    CHECK(table.Observe(100,9,2,&replacement));
    CHECK(replacement.world!=a.world && !table.Resolve(a,&ref));
    CHECK(table.Resolve(replacement,&ref));

    CeCommandLifetimeTable<2> reload;
    CHECK(reload.Observe(150,12,1,&a));
    reload.EndWorld();
    CHECK(reload.Observe(150,12,1,&b));
    CHECK(a.generation==b.generation && a.id==b.id && a.world!=b.world);
    CHECK(!reload.Resolve(a,&ref) && reload.Resolve(b,&ref));

    CeCommandLifetimeTable<1> bounded;
    CHECK(bounded.Observe(300,7,1,&a));
    CHECK(!bounded.Observe(400,8,1,&b));
    CHECK(b.world==0 && b.generation==0);
    CHECK(bounded.Resolve(a,&ref) && ref.address==300 && bounded.Size()==1);
    CHECK(bounded.Forget(300));
    CHECK(bounded.Observe(400,8,1,&b));
    CHECK(b.generation!=a.generation && !bounded.Resolve(a,&ref));

    const auto last=(std::numeric_limits<uint64_t>::max)();
    CeCommandLifetimeTable<2> serial(2,last);
    CHECK(serial.Observe(500,3,1,&a) && a.generation==last);
    CHECK(!serial.Observe(600,4,1,&b));
    CHECK(serial.Observe(500,3,1,&b) && b.generation==a.generation);
    CHECK(serial.Resolve(a,&ref));
    CHECK(serial.Forget(500));
    CHECK(!serial.Observe(500,3,1,&b));
    serial.EndWorld();
    CHECK(serial.Observe(500,3,1,&b) && b.world!=a.world);
    CHECK(!serial.Resolve(a,&ref));
    CeCommandLifetimeTable<2> epoch(last);
    CHECK(epoch.Observe(700,5,1,&a));
    epoch.EndWorld();
    CHECK(epoch.World()==0 && !epoch.Resolve(a,&ref));
    CHECK(!epoch.Observe(700,5,1,&b));
    epoch.EndWorld();
    CHECK(epoch.World()==0 && !epoch.Observe(700,5,1,&b));

    CHECK(staticTable.Observe(900,6,1,&a));
    staticTable.EndWorld();
    CHECK(!staticTable.Forget(900) && !staticTable.Resolve(a,&ref));
    CHECK(!staticTable.Forget(0));
    for (int i=0;i<100;++i) {
        CHECK(staticTable.Observe(900,6,1,&a));
        CHECK(staticTable.Forget(900));
        CHECK(!staticTable.Resolve(a,&ref));
        CHECK(staticTable.Observe(900,6,1,&b));
        CHECK(a.generation!=b.generation && !staticTable.Resolve(a,&ref));
        CHECK(staticTable.Forget(900) && !staticTable.Forget(900));
    }
    std::printf("lifetime checks=%d failures=0 skips=0\n",checks);
    return 0;
}
