#include "virtualization/vm/VirtualMachineNic.hpp"

std::string VirtualMachineNic::generate_mac() {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<int> dis(0x00, 0xFF);

  std::ostringstream oss;
  oss << std::hex << std::setfill('0');

  // أول 3 بايتات ثابتة لمتوافقة KVM/QEMU
  oss << "52:54:00";

  // 3 بايتات عشوائية
  for (int i = 0; i < 3; ++i) {
    int byte = dis(gen);

    // نضمن أن البايت الأخير ليس multicast
    if (i == 0) {
      byte &= 0xFE; // آخر bit = 0 → ليس multicast
      byte |= 0x02; // second bit = 1 → locally administered
    }

    oss << ":" << std::setw(2) << byte;
  }

  this->mac = oss.str();
  
}