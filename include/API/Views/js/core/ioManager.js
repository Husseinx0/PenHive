/**
 * IOManager - مدير الإدخال والإخراج
 * إدارة حفظ وتحمل المشاريع
 */
class IOManager {
    constructor(designer) {
        this.designer = designer;
        this.setupIOButtons();
    }

    /**
     * إعداد أزرار الإدخال والإخراج
     */
    setupIOButtons() {
        if (this.designer.saveBtn) {
            this.designer.saveBtn.addEventListener('click', () => this.exportProject());
        }
        
        if (this.designer.loadBtn && this.designer.loadFileInput) {
            this.designer.loadBtn.addEventListener('click', () => this.designer.loadFileInput.click());
            this.designer.loadFileInput.addEventListener('change', (e) => this.handleFileLoad(e));
        }
    }

    /**
     * تصدير المشروع
     */
    exportProject() {
        const data = {
            version: '2.0',
            timestamp: new Date().toISOString(),
            devices: Array.from(this.designer.devices.values()).map(d => ({
                id: d.id,
                type: d.type,
                x: d.x,
                y: d.y
            })),
            cables: Array.from(this.designer.cables.values()).map(c => ({
                id: c.id,
                from: c.from,
                to: c.to
            }))
        };

        const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        
        a.href = url;
        a.download = `cyperland-project-${new Date().toISOString().split('T')[0]}.json`;
        a.click();
        
        URL.revokeObjectURL(url);
        
        this.showNotification('تم حفظ المشروع بنجاح', 'success');
    }

    /**
     * معالجة تحميل الملف
     */
    handleFileLoad(e) {
        const file = e.target.files[0];
        if (!file) return;

        const reader = new FileReader();
        reader.onload = (event) => {
            try {
                this.importProject(event.target.result);
                this.showNotification('تم تحميل المشروع بنجاح', 'success');
            } catch (error) {
                console.error('Error loading project:', error);
                this.showNotification('خطأ في تحميل المشروع', 'error');
            }
        };
        
        reader.onerror = () => {
            this.showNotification('خطأ في قراءة الملف', 'error');
        };
        
        reader.readAsText(file);
        
        e.target.value = '';
    }

    /**
     * استيراد المشروع
     */
    importProject(jsonStr) {
        const data = JSON.parse(jsonStr);
        
        if (!data.version || !data.devices) {
            throw new Error('تنسيق الملف غير صالح');
        }

        this.clearWorkspace();

        data.devices.forEach(deviceData => {
            this.designer.createDevice(deviceData.type, deviceData.x, deviceData.y);
        });

        setTimeout(() => {
            if (data.cables) {
                data.cables.forEach(cableData => {
                    this.designer.createCableBetween(cableData.from, cableData.to);
                });
            }
        }, 100);
    }

    /**
     * مسح مساحة العمل
     */
    clearWorkspace() {
        for (const id of Array.from(this.designer.cables.keys())) {
            this.designer.removeCable(id);
        }
        
        for (const id of Array.from(this.designer.devices.keys())) {
            this.designer.removeDevice(id);
        }
    }

    /**
     * عرض الإشعارات
     */
    showNotification(message, type = 'info') {
        const notification = document.createElement('div');
        notification.className = `app-notification alert alert-${type} alert-dismissible fade show`;
        notification.innerHTML = `
            ${message}
            <button type="button" class="btn-close" data-bs-dismiss="alert"></button>
        `;

        Object.assign(notification.style, {
            position: 'fixed',
            top: '20px',
            right: '20px',
            zIndex: 1060,
            minWidth: '300px'
        });

        document.body.appendChild(notification);

        setTimeout(() => {
            if (notification.parentNode) {
                notification.remove();
            }
        }, 5000);
    }

    /**
     * تصدير كصورة
     */
    exportAsImage() {
        this.showNotification('جاري تصدير الصورة...', 'info');
        
        setTimeout(() => {
            this.showNotification('وظيفة التصدير كصورة قيد التطوير', 'warning');
        }, 1000);
    }

    /**
     * إنشاء مشروع جديد
     */
    createNewProject() {
        if (this.designer.devices.size > 0) {
            if (!confirm('هل تريد إنشاء مشروع جديد؟ سيتم فقدان جميع التغييرات غير المحفوظة.')) {
                return;
            }
        }
        
        this.clearWorkspace();
        this.showNotification('تم إنشاء مشروع جديد', 'success');
    }
}