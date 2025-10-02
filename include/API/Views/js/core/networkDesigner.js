/**
 * NetworkDesigner - الفئة الرئيسية لتطبيق محاكاة الشبكة
 * إدارة مساحة العمل، الأجهزة، الاتصالات، والحالة العامة
 */
class NetworkDesigner {
    constructor() {
        this.GRID_SIZE = 20;
        this.workspace = document.getElementById('workspace').querySelector('.workspace-container');
        this.devicesPanel = document.getElementById('devices-panel');
        this.deviceLibraryItems = document.querySelectorAll('.device-item');
        this.loadFileInput = document.getElementById('loadFile');
        this.saveBtn = document.getElementById('saveProject');
        this.loadBtn = document.getElementById('loadProject');
        this.placeholder = document.getElementById('workspace-placeholder');
        
        // الحاويات
        this.devices = new Map();
        this.cables = new Map();

        // الحالة
        this.deviceCounter = 0;
        this.cableCounter = 0;
        this.selectedCableId = null;
        this.zoomLevel = 1;

        // حالة الاتصال المحسنة
        this.connectionState = {
            isConnecting: false,
            startDevice: null,
            tempLine: null,
            cableTool: null
        };

        // SVG overlay
        this.svgNS = "http://www.w3.org/2000/svg";
        this.svg = this.createSvgOverlay();

        // المُديرون
        this.ioManager = new IOManager(this);
        this.rpcService = new XmlRPCService();
        
        this.init();
    }

    /**
     * تهيئة التطبيق
     */
    init() {
        this.setupEventListeners();
        this.setupDeviceLibrary();
        this.setupSystemMonitoring();
        console.log('NetworkDesigner: Initialized');
    }

    /**
     * إنشاء طبقة SVG للاتصالات
     */
    createSvgOverlay() {
        const svg = document.createElementNS(this.svgNS, 'svg');
        Object.assign(svg.style, {
            position: 'absolute',
            left: 0,
            top: 0,
            width: '100%',
            height: '100%',
            pointerEvents: 'none',
            zIndex: 4
        });
        this.workspace.appendChild(svg);
        return svg;
    }

    /**
     * إعداد مستمعي الأحداث
     */
    setupEventListeners() {
        window.addEventListener('resize', () => this.redrawAllCables());
        window.addEventListener('keydown', (e) => this.handleKeydown(e));
        window.addEventListener('mousemove', (e) => this.trackConnection(e));
        window.addEventListener('mouseup', () => this.endDrag());

        this.workspace.addEventListener('click', (e) => this.handleWorkspaceClick(e));
        this.workspace.addEventListener('dragover', (e) => e.preventDefault());
        this.workspace.addEventListener('drop', (e) => this.handleDrop(e));

        // أحداث التكبير والتصغير
        document.getElementById('zoomIn').addEventListener('click', () => this.zoom(0.1));
        document.getElementById('zoomOut').addEventListener('click', () => this.zoom(-0.1));
        document.getElementById('resetWorkspace').addEventListener('click', () => this.resetWorkspace());
        document.getElementById('darkModeToggle').addEventListener('change', (e) => this.toggleDarkMode(e));
    }

    /**
     * إعداد مكتبة الأجهزة
     */
    setupDeviceLibrary() {
        this.deviceLibraryItems.forEach(item => {
            item.style.opacity = '1';
            item.classList.add('animated');

            item.addEventListener('dragstart', (e) => {
                e.dataTransfer.setData('text/plain', item.dataset.type || 'pc');
                this.workspace.classList.add('drag-over');
                setTimeout(() => item.style.opacity = '0.6', 0);
            });

            item.addEventListener('dragend', () => {
                this.workspace.classList.remove('drag-over');
                item.style.opacity = '1';
            });

            item.addEventListener('dblclick', () => {
                const { width, height } = this.workspace.getBoundingClientRect();
                const x = (width - 65) / 2;
                const y = (height - 65) / 2;
                this.createDevice(item.dataset.type || 'pc', x, y);
            });
        });
    }

    /**
     * إعداد مراقبة النظام
     */
    setupSystemMonitoring() {
        setInterval(() => {
            this.updateSystemStats();
        }, 2000);
    }

    /**
     * تحديث إحصائيات النظام
     */
    updateSystemStats() {
        const cpuUsage = Math.floor(Math.random() * 80) + 10;
        const ramUsage = Math.floor(Math.random() * 70) + 15;
        const deviceCount = this.devices.size;

        document.getElementById('cpu').textContent = `${cpuUsage}%`;
        document.getElementById('ram').textContent = `${ramUsage}%`;
        document.getElementById('device-count').textContent = deviceCount;

        document.getElementById('cpu-progress').style.width = `${cpuUsage}%`;
        document.getElementById('ram-progress').style.width = `${ramUsage}%`;
    }

    /**
     * معالجة إفلات الجهاز
     */
    handleDrop(e) {
        e.preventDefault();
        const type = e.dataTransfer.getData('text/plain') || 'pc';
        const rect = this.workspace.getBoundingClientRect();
        const x = e.clientX - rect.left - 32;
        const y = e.clientY - rect.top - 32;
        const snapped = this.snapToGrid(x, y);
        this.createDevice(type, snapped.x, snapped.y);
    }

    /**
     * محاذاة إلى الشبكة
     */
    snapToGrid(x, y) {
        return {
            x: Math.round(x / this.GRID_SIZE) * this.GRID_SIZE,
            y: Math.round(y / this.GRID_SIZE) * this.GRID_SIZE
        };
    }

    /**
     * الحصول على مركز الجهاز
     */
    getDeviceCenter(el) {
        const wsRect = this.workspace.getBoundingClientRect();
        const r = el.getBoundingClientRect();
        return {
            x: r.left - wsRect.left + r.width / 2,
            y: r.top - wsRect.top + r.height / 2
        };
    }

    /**
     * إنشاء جهاز جديد
     */
    async createDevice(type, x, y) {
        const id = `device-${++this.deviceCounter}`;
        const device = new Device(id, type, x, y, this);
        
        device.showLoading();
        this.devices.set(id, device);
        this.updatePlaceholder();

        try {
            await this.simulateDeviceCreation(device);
        } catch (error) {
            console.error('Failed to create device:', error);
            device.handleCreationFailure();
        }
    }

    /**
     * محاكاة إنشاء الجهاز
     */
    async simulateDeviceCreation(device) {
        return new Promise((resolve, reject) => {
            const willFail = Math.random() < 0.1;
            const creationTime = 1000 + Math.random() * 1000;

            setTimeout(() => {
                if (willFail) {
                    reject(new Error('Failed to create device'));
                } else {
                    device.hideLoading();
                    resolve();
                }
            }, creationTime);
        });
    }

    /**
     * تحديث المكان النائب
     */
    updatePlaceholder() {
        if (this.devices.size === 0) {
            this.placeholder.style.opacity = '1';
            this.placeholder.style.pointerEvents = 'auto';
        } else {
            this.placeholder.style.opacity = '0';
            this.placeholder.style.pointerEvents = 'none';
        }
    }

    /**
     * إزالة جهاز
     */
    removeDevice(id) {
        const device = this.devices.get(id);
        if (!device) return;

        // حذف الكابلات المرتبطة
        for (const [cid, cable] of this.cables) {
            if (cable.from === id || cable.to === id) {
                this.removeCable(cid);
            }
        }

        this.devices.delete(id);
        if (device.el && device.el.parentNode) {
            device.el.parentNode.removeChild(device.el);
        }
        
        this.updatePlaceholder();
    }

    /**
     * بدء عملية الاتصال
     */
    startConnection(deviceId) {
        if (this.connectionState.isConnecting) {
            this.cancelConnection();
            return;
        }

        const device = this.devices.get(deviceId);
        if (!device) return;

        this.connectionState.isConnecting = true;
        this.connectionState.startDevice = deviceId;

        // إنشاء خط مؤقت للاتصال
        this.connectionState.tempLine = document.createElementNS(this.svgNS, 'line');
        this.connectionState.tempLine.setAttribute('class', 'connection-line temp');
        this.connectionState.tempLine.setAttribute('stroke-width', '3');
        this.connectionState.tempLine.setAttribute('stroke-dasharray', '6 6');
        this.svg.appendChild(this.connectionState.tempLine);

        // إنشاء أداة الكابل المرئية
        this.createCableTool();

        // تحديث واجهة المستخدم
        this.workspace.classList.add('connecting-mode');
        document.body.style.cursor = 'crosshair';

        this.updateConnectionLine();

        console.log(`بدء الاتصال من: ${deviceId}`);
    }

    /**
     * إنهاء عملية الاتصال
     */
    finishConnection(targetDeviceId) {
        if (!this.connectionState.isConnecting) return;

        const startId = this.connectionState.startDevice;
        
        if (startId && startId !== targetDeviceId) {
            this.createCableBetween(startId, targetDeviceId);
        }

        this.cleanupConnection();
    }

    /**
     * إلغاء عملية الاتصال
     */
    cancelConnection() {
        if (this.connectionState.isConnecting) {
            console.log('إلغاء عملية الاتصال');
            this.cleanupConnection();
        }
    }

    /**
     * تنظيف حالة الاتصال
     */
    cleanupConnection() {
        // إزالة الخط المؤقت
        if (this.connectionState.tempLine && this.connectionState.tempLine.parentNode) {
            this.connectionState.tempLine.parentNode.removeChild(this.connectionState.tempLine);
        }

        // إزالة أداة الكابل
        if (this.connectionState.cableTool && this.connectionState.cableTool.parentNode) {
            this.connectionState.cableTool.parentNode.removeChild(this.connectionState.cableTool);
        }

        // إعادة تعيين الحالة
        this.connectionState.isConnecting = false;
        this.connectionState.startDevice = null;
        this.connectionState.tempLine = null;
        this.connectionState.cableTool = null;

        // إعادة تعيين واجهة المستخدم
        this.workspace.classList.remove('connecting-mode');
        document.body.style.cursor = '';

        console.log('تم تنظيف حالة الاتصال');
    }

    /**
     * إنشاء أداة الكابل المرئية
     */
    createCableTool() {
        this.connectionState.cableTool = document.createElement('div');
        this.connectionState.cableTool.className = 'cable-tool';
        this.connectionState.cableTool.innerHTML = '<i class="fas fa-plug"></i>';
        this.connectionState.cableTool.title = 'اسحب إلى جهاز آخر للاتصال (ESC للإلغاء)';
        
        document.body.appendChild(this.connectionState.cableTool);
    }

    /**
     * تحديث خط الاتصال المؤقت
     */
    updateConnectionLine(mouseX, mouseY) {
        if (!this.connectionState.isConnecting || !this.connectionState.tempLine) return;

        const startDevice = this.devices.get(this.connectionState.startDevice);
        if (!startDevice) {
            this.cleanupConnection();
            return;
        }

        const startCenter = this.getDeviceCenter(startDevice.el);

        if (mouseX !== undefined && mouseY !== undefined) {
            this.connectionState.tempLine.setAttribute('x1', startCenter.x);
            this.connectionState.tempLine.setAttribute('y1', startCenter.y);
            this.connectionState.tempLine.setAttribute('x2', mouseX);
            this.connectionState.tempLine.setAttribute('y2', mouseY);

            if (this.connectionState.cableTool) {
                this.connectionState.cableTool.style.left = (mouseX - 16) + 'px';
                this.connectionState.cableTool.style.top = (mouseY - 16) + 'px';
                this.connectionState.cableTool.classList.add('active');
            }
        } else {
            this.connectionState.tempLine.setAttribute('x1', startCenter.x);
            this.connectionState.tempLine.setAttribute('y1', startCenter.y);
            this.connectionState.tempLine.setAttribute('x2', startCenter.x);
            this.connectionState.tempLine.setAttribute('y2', startCenter.y);
        }
    }

    /**
     * تتبع حركة الماوس أثناء الاتصال
     */
    trackConnection(e) {
        if (!this.connectionState.isConnecting) return;

        const wsRect = this.workspace.getBoundingClientRect();
        const mouseX = e.clientX - wsRect.left;
        const mouseY = e.clientY - wsRect.top;

        this.updateConnectionLine(mouseX, mouseY);
    }

    /**
     * معالجة النقر على مساحة العمل
     */
    handleWorkspaceClick(e) {
        if (this.connectionState.isConnecting && 
            (e.target === this.workspace || e.target.classList.contains('workspace-container'))) {
            this.cancelConnection();
        } else {
            this.clearSelection();
        }
    }

    /**
     * التحقق من إمكانية الاتصال بين جهازين
     */
    canConnectDevices(fromId, toId) {
        if (fromId === toId) {
            this.showNotification('لا يمكن الاتصال من الجهاز إلى نفسه', 'warning');
            return false;
        }

        for (const [, cable] of this.cables) {
            if ((cable.from === fromId && cable.to === toId) || 
                (cable.from === toId && cable.to === fromId)) {
                this.showNotification('الاتصال موجود مسبقاً بين هذين الجهازين', 'warning');
                return false;
            }
        }

        return true;
    }

    /**
     * إنشاء كابل بين جهازين مع التحقق
     */
    createCableBetween(fromId, toId) {
        if (!this.canConnectDevices(fromId, toId)) {
            return null;
        }

        const id = `cable-${++this.cableCounter}`;
        const cable = new Cable(id, fromId, toId, this);
        
        if (cable.isValid()) {
            this.cables.set(id, cable);
            this.selectCable(id);
            this.showNotification('تم إنشاء الاتصال بنجاح', 'success');
            return cable;
        } else {
            cable.remove();
            this.showNotification('فشل في إنشاء الاتصال', 'error');
            return null;
        }
    }

    /**
     * إزالة كابل
     */
    removeCable(id) {
        const cable = this.cables.get(id);
        if (!cable) return;

        cable.remove();
        this.cables.delete(id);

        if (this.selectedCableId === id) {
            this.selectedCableId = null;
        }
    }

    /**
     * تحديد كابل
     */
    selectCable(id) {
        if (this.selectedCableId && this.cables.has(this.selectedCableId)) {
            const prev = this.cables.get(this.selectedCableId);
            prev.deselect();
        }
        this.selectedCableId = id;
        const cable = this.cables.get(id);
        if (cable) cable.select();
    }

    /**
     * مسح التحديد
     */
    clearSelection() {
        if (this.selectedCableId) {
            const cable = this.cables.get(this.selectedCableId);
            if (cable) cable.deselect();
            this.selectedCableId = null;
        }
    }

    /**
     * معالجة ضغطات المفاتيح
     */
    handleKeydown(e) {
        if (e.key === 'Delete' && this.selectedCableId) {
            this.removeCable(this.selectedCableId);
        }
        
        if (e.key === 'Escape' && this.connectionState.isConnecting) {
            this.cancelConnection();
        }
        
        if (e.key === 'Escape' && this.selectedCableId) {
            this.clearSelection();
        }
    }

    /**
     * إعادة رسم جميع الكابلات
     */
    redrawAllCables() {
        for (const [, cable] of this.cables) {
            cable.position();
        }
    }

    /**
     * إعادة رسم الكابلات المرتبطة بجهاز
     */
    redrawCablesForDevice(deviceId) {
        for (const [, cable] of this.cables) {
            if (cable.from === deviceId || cable.to === deviceId) {
                cable.position();
            }
        }
    }

    /**
     * نهاية السحب
     */
    endDrag() {
        for (const [, device] of this.devices) {
            if (device.isDragging) {
                const snapped = this.snapToGrid(parseFloat(device.el.style.left), parseFloat(device.el.style.top));
                device.el.style.left = snapped.x + 'px';
                device.el.style.top = snapped.y + 'px';
                device.x = snapped.x;
                device.y = snapped.y;
                this.redrawCablesForDevice(device.id);
                device.isDragging = false;
            }
        }
    }

    /**
     * التكبير والتصغير
     */
    zoom(delta) {
        this.zoomLevel = Math.max(0.5, Math.min(2, this.zoomLevel + delta));
        this.workspace.style.transform = `scale(${this.zoomLevel})`;
        this.workspace.style.transformOrigin = 'center center';
    }

    /**
     * إعادة تعيين مساحة العمل
     */
    resetWorkspace() {
        if (confirm('هل تريد إعادة تعيين مساحة العمل؟ سيتم فقدان جميع التغييرات غير المحفوظة.')) {
            for (const id of Array.from(this.cables.keys())) this.removeCable(id);
            for (const id of Array.from(this.devices.keys())) this.removeDevice(id);
            this.zoomLevel = 1;
            this.workspace.style.transform = 'scale(1)';
        }
    }

    /**
     * تبديل الوضع الداكن
     */
    toggleDarkMode(e) {
        document.body.classList.toggle('dark-mode', e.target.checked);
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
        }, 4000);
    }
}