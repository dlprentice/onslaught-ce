// SPDX-License-Identifier: GPL-3.0-only
// Authored RTTI guards, compiled in the unchanged game's VC6-compatible /QIfist mode.
#include "typeinfo_bridge.h"
#include <stdio.h>
#include <string.h>

static unsigned failures, checks;
static void Check(bool ok,const char *label)
{
	checks++;if(!ok) { fprintf(stderr,"failed RTTI guard: %s\n",label);failures++; }
}
struct Base { virtual ~Base() {} };
struct Supported : Base {};
struct Unsupported : Base {};
struct DerivedUnsupported : Supported {};
static bool Equal(const std::type_info &a,const std::type_info &b)
{
#ifdef TEST_DIRECT_RTTI
	return a==b;
#else
	return PortTypeInfoEqual(a,b);
#endif
}
__attribute__((noinline)) static bool Exact(Base *value)
{
	return value && Equal(typeid(*value),typeid(Supported));
}
#ifdef __GLIBCXX__
// The selected libstdc++ ABI exposes this protected constructor. Duplicate name storage represents nonmerged RTTI;
// leading '*' marks names that cannot establish equality across distinct descriptors. Public name() strips it.
struct Descriptor : std::type_info { explicit Descriptor(const char *name):std::type_info(name) {} };
#endif
int main()
{
	Base base;Supported a,b;Unsupported other;DerivedUnsupported derived;
	Check(Exact(&a),"supported dynamic type");Check(!Exact(&base),"base rejected");
	Check(!Exact(&other),"sibling rejected");Check(!Exact(&derived),"derived subclass remains unsupported");
	Check(dynamic_cast<Supported *>(&derived)!=0,"derived dynamic_cast control");Check(!Exact(0),"null guarded before typeid");
	Check(Equal(typeid(a),typeid(b)),"same compiler type");Check(!Equal(typeid(a),typeid(other)),"distinct compiler types");
	Check(Equal(typeid(a),typeid(const Supported)),"cv-qualified type identity");
#ifdef __GLIBCXX__
	char first[]="SharedDescriptor",second[]="SharedDescriptor",different[]="OtherDescriptor";
	Descriptor x(first),y(second),z(different);
	Check(first!=second,"nonmerged name-storage control");Check(&x!=&y,"nonmerged descriptor control");
	Check(Equal(x,y) && Equal(y,x),"equal nonmerged names remain equal");Check(!Equal(x,z),"different nonmerged names");
	char local1[]="*LocalDescriptor",local2[]="*LocalDescriptor";Descriptor p(local1),q(local2);
	Check(!strcmp(p.name(),q.name()),"public names hide local marker");
	Check(Equal(p,p),"same local descriptor");Check(!Equal(p,q) && !Equal(q,p),"distinct local descriptors remain different");
#endif
	printf("RTTI checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
