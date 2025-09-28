# استخدام صورة أساسية تحتوي على أدوات التطوير
FROM ubuntu:22.04

# تثبيت الأدوات اللازمة
RUN apt-get update && \
    apt-get install -y \
    g++ \
    cmake \
    make \
    git \
    && rm -rf /var/lib/apt/lists/*

# تعيين مجلد العمل
WORKDIR /app

# نسخ جميع ملفات المشروع إلى الحاوية
COPY . .

# إنشاء مجلد build والتحويل إليه
RUN mkdir build && cd build

# تشغيل cmake و make
RUN cd build && \
    cmake .. && \
    make

# الأمر الافتراضي (يمكن تعديله حسب احتياجك)
CMD ["./build/YourExecutableName"]