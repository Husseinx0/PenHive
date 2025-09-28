#include <iostream>
#include <libvirt/libvirt.h>
#include <libvirt/libvirt-lxc.h>

int main() {
    // الاتصال بالـ hypervisor المحلي
    virConnectPtr conn = virConnectOpen("qemu:///system");
    if (!conn) {
        std::cerr << "فشل الاتصال بالـ hypervisor." << std::endl;
        return 1;
    }

    // الحصول على عدد الأجهزة الافتراضية
    int numDomains = virConnectNumOfDomains(conn);
    if (numDomains < 0) {
        std::cerr << "فشل الحصول على عدد الأجهزة الافتراضية." << std::endl;
        virConnectClose(conn);
        return 1;
    }

    // قائمة لتخزين معرفات الأجهزة
    int* activeDomains = new int[numDomains];
    int count = virConnectListDomains(conn, activeDomains, numDomains);

    std::cout << "عدد الأجهزة الافتراضية النشطة: " << count << std::endl;
    for (int i = 0; i < count; i++) {
        virDomainPtr dom = virDomainLookupByID(conn, activeDomains[i]);
        if (dom) {
            std::string name = virDomainGetName(dom);
            std::cout << "اسم الجهاز: " << name << std::endl;
            virDomainFree(dom);
        }
    }

    delete[] activeDomains;

    // الأجهزة المعرفة ولكن غير نشطة
    int inactiveNum = virConnectNumOfDefinedDomains(conn);
    std::cout << "عدد الأجهزة المعرفة وغير النشطة: " << inactiveNum << std::endl;

    char** definedDomains = new char*[inactiveNum];
    virConnectListDefinedDomains(conn, definedDomains, inactiveNum);
    for (int i = 0; i < inactiveNum; i++) {
        std::cout << "اسم الجهاز غير النشط: " << definedDomains[i] << std::endl;
        free(definedDomains[i]);
    }
    delete[] definedDomains;

    virConnectClose(conn);
    return 0;
}
