/**
 * Device - فئة تمثل جهاز في الشبكة
 * إدارة إنشاء وعرض وسلوك الأجهزة
 */
class Device {
    constructor(id, type, x, y, designer) {
        this.id = id;
        this.type = type;
        this.x = x;
        this.y = y;
        this.designer = designer;
        this.isDragging = false;
        
        this.el = this.createElement();
        this.designer.workspace.appendChild(this.el);
        
        this.enableDrag();
        this.addEventListeners();
        this.createLabel();
        this.createOSBadge();
    }

    /**
     * إنشاء عنصر الجهاز
     */
    createElement() {
        const el = document.createElement('div');
        el.className = 'device-icon animated';
        el.dataset.id = this.id;
        el.dataset.type = this.type;

        Object.assign(el.style, {
            position: 'absolute',
            left: this.x + 'px',
            top: this.y + 'px',
            width: '65px',
            height: '65px',
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'center',
            opacity: '1',
            zIndex: 10,
            borderRadius: '12px',
            background: 'white',
            border: '2px solid transparent',
            userSelect: 'none'
        });

        const icon = this.createIcon();
        el.appendChild(icon);

        return el;
    }

    /**
     * إنشاء أيقونة الجهاز
     */
    createIcon() {
        const i = document.createElement('i');
        i.style.fontSize = '26px';
        i.style.opacity = '0';
        i.style.transition = 'opacity 0.3s ease';

        const iconMap = {
            router: 'fas fa-wifi router-icon',
            switch: 'fas fa-network-wired switch-icon',
            pc: 'fas fa-desktop pc-icon',
            server: 'fas fa-server server-icon',
            laptop: 'fas fa-laptop laptop-icon',
            tablet: 'fas fa-tablet-alt pc-icon',
            wireless: 'fas fa-wifi wireless-icon',
            cable: 'fas fa-plug cable-icon'
        };

        i.className = iconMap[this.type] || 'fas fa-question';
        return i;
    }

    /**
     * إنشاء تسمية الجهاز
     */
    createLabel() {
        this.label = document.createElement('div');
        this.label.className = 'device-label';
        this.label.textContent = this.generateName();
        this.el.appendChild(this.label);
    }

    /**
     * إنشاء شارة نظام التشغيل
     */
    createOSBadge() {
        const osInfo = this.getOSInfo();

        this.badge = document.createElement('div');
        this.badge.className = 'device-os-badge';
        this.badge.innerHTML = `
            <span class="os-name">${osInfo.name}</span>
            <span class="os-icon">${osInfo.icon}</span>
        `;

        Object.assign(this.badge.style, {
            background: osInfo.color
        });

        this.el.appendChild(this.badge);
    }

    /**
     * الحصول على معلومات نظام التشغيل
     */
    getOSInfo() {
        const osMap = {
            pc: { name: 'Windows 10', icon: '🪟', color: '#0078D7' },
            laptop: { name: 'Ubuntu', icon: '🐧', color: '#E95420' },
            server: { name: 'CentOS', icon: '🎩', color: '#932279' },
            router: { name: 'RouterOS', icon: '🔄', color: '#00AFF0' },
            switch: { name: 'Cisco IOS', icon: '🔌', color: '#1BA0D7' },
            tablet: { name: 'Android', icon: '🤖', color: '#3DDC84' },
            wireless: { name: 'OpenWRT', icon: '📶', color: '#8C8C8C' }
        };

        return osMap[this.type] || { name: 'Unknown OS', icon: '❓', color: '#666666' };
    }

    /**
     * توليد اسم الجهاز
     */
    generateName() {
        const names = {
            router: 'راوتر',
            switch: 'سويتش',
            pc: 'كمبيوتر',
            server: 'خادم',
            laptop: 'لابتوب',
            tablet: 'جهاز لوحي',
            wireless: 'واي فاي',
            cable: 'كابل'
        };
        return names[this.type] || 'جهاز';
    }

    /**
     * عرض حالة التحميل
     */
    showLoading() {
        const loadingDiv = document.createElement('div');
        loadingDiv.className = 'device-loading';
        
        const spinner = document.createElement('div');
        spinner.className = 'device-loading-spinner';
        loadingDiv.appendChild(spinner);

        this.el.appendChild(loadingDiv);
    }

    /**
     * إخفاء حالة التحميل
     */
    hideLoading() {
        const loading = this.el.querySelector('.device-loading');
        const icon = this.el.querySelector('i');

        if (loading) {
            loading.style.transition = 'opacity 0.3s ease';
            loading.style.opacity = '0';
            setTimeout(() => loading.remove(), 300);
        }
        
        if (icon) {
            icon.style.opacity = '1';
        }

        this.el.classList.add('device-pop-in');
        setTimeout(() => this.el.classList.remove('device-pop-in'), 300);
    }

    /**
     * معالجة فشل الإنشاء
     */
    handleCreationFailure() {
        const loading = this.el.querySelector('.device-loading');
        if (loading) loading.remove();

        const failureDiv = document.createElement('div');
        failureDiv.className = 'device-failure';
        failureDiv.innerHTML = '✕';
        this.el.appendChild(failureDiv);
        this.el.style.animation = 'shake 0.5s ease-in-out';

        setTimeout(() => {
            this.el.classList.add('fade-out');
            setTimeout(() => this.designer.removeDevice(this.id), 500);
        }, 1500);
    }

    /**
     * إضافة مستمعي الأحداث
     */
    addEventListeners() {
        this.el.addEventListener('click', (e) => {
            if (e.shiftKey) {
                e.stopPropagation();
                this.onShiftClick();
            }
        });

        this.el.addEventListener('contextmenu', (e) => {
            e.preventDefault();
            this.showContextMenu(e);
        });

        this.el.addEventListener('mouseenter', () => this.handleHoverDuringConnection());
        this.el.addEventListener('mouseleave', () => this.handleLeaveDuringConnection());
    }

    /**
     * معالجة النقر للاتصال المحسنة
     */
    onShiftClick() {
        if (!this.designer.connectionState.isConnecting) {
            this.designer.startConnection(this.id);
            this.showConnectionHint();
        } else if (this.designer.connectionState.startDevice === this.id) {
            this.designer.cancelConnection();
        } else {
            this.designer.finishConnection(this.id);
        }
    }

    /**
     * عرض تلميح الاتصال
     */
    showConnectionHint() {
        this.el.classList.add('connecting-source');
        
        setTimeout(() => {
            this.el.classList.remove('connecting-source');
        }, 3000);
    }

    /**
     * معالجة مرور الماوس أثناء وضع الاتصال
     */
    handleHoverDuringConnection() {
        if (this.designer.connectionState.isConnecting && 
            this.designer.connectionState.startDevice !== this.id) {
            this.el.classList.add('connection-target');
        }
    }

    /**
     * معالجة مغادرة الماوس أثناء وضع الاتصال
     */
    handleLeaveDuringConnection() {
        this.el.classList.remove('connection-target');
    }

    /**
     * عرض قائمة السياق
     */
    showContextMenu(e) {
        const existingMenu = document.querySelector('.device-context-menu');
        if (existingMenu) existingMenu.remove();

        const menu = document.createElement('div');
        menu.className = 'device-context-menu';
        menu.innerHTML = `
            <div class="context-item" data-action="connect">
                اتصال
                <small class="text-muted">(Shift+Click)</small>
            </div>
            <div class="context-item" data-action="properties">خصائص</div>
            <div class="context-item" data-action="delete">حذف</div>
        `;

        Object.assign(menu.style, {
            position: 'fixed',
            left: e.pageX + 'px',
            top: e.pageY + 'px',
            background: 'white',
            border: '1px solid #ddd',
            borderRadius: '8px',
            boxShadow: '0 4px 12px rgba(0,0,0,0.15)',
            zIndex: 1000,
            minWidth: '150px'
        });

        document.body.appendChild(menu);

        menu.querySelectorAll('.context-item').forEach(item => {
            item.addEventListener('click', () => {
                this.handleContextAction(item.dataset.action);
                menu.remove();
            });
        });

        setTimeout(() => {
            const removeMenu = (e) => {
                if (!menu.contains(e.target)) {
                    menu.remove();
                    document.removeEventListener('click', removeMenu);
                }
            };
            document.addEventListener('click', removeMenu);
        }, 0);
    }

    /**
     * معالجة إجراءات قائمة السياق
     */
    handleContextAction(action) {
        switch (action) {
            case 'connect':
                this.designer.startConnection(this.id);
                break;
            case 'properties':
                this.showProperties();
                break;
            case 'delete':
                if (confirm('هل تريد حذف هذا الجهاز؟')) {
                    this.designer.removeDevice(this.id);
                }
                break;
        }
    }

    /**
     * عرض خصائص الجهاز
     */
    showProperties() {
        const propertiesContent = document.getElementById('device-properties-content');
        propertiesContent.innerHTML = `
            <div class="properties-header mb-3">
                <h6 class="fw-bold">${this.generateName()}</h6>
                <small class="text-muted">${this.id}</small>
            </div>
            <div class="properties-list">
                <div class="property-item mb-2">
                    <strong>النوع:</strong> ${this.type}
                </div>
                <div class="property-item mb-2">
                    <strong>الموقع:</strong> ${Math.round(this.x)}, ${Math.round(this.y)}
                </div>
                <div class="property-item mb-2">
                    <strong>نظام التشغيل:</strong> ${this.getOSInfo().name}
                </div>
                <div class="property-item">
                    <strong>الحالة:</strong> <span class="text-success">● نشط</span>
                </div>
            </div>
        `;
    }

    /**
     * تمكين السحب
     */
    enableDrag() {
        let startX, startY, origLeft, origTop;

        this.el.addEventListener('mousedown', (e) => {
            if (e.button !== 0) return;
            
            this.isDragging = true;
            this.el.classList.add('dragging');
            startX = e.clientX;
            startY = e.clientY;
            
            const rect = this.el.getBoundingClientRect();
            const wsRect = this.designer.workspace.getBoundingClientRect();
            origLeft = rect.left - wsRect.left;
            origTop = rect.top - wsRect.top;
            
            document.body.style.userSelect = 'none';
        });

        window.addEventListener('mousemove', (e) => {
            if (!this.isDragging) return;
            
            const dx = e.clientX - startX;
            const dy = e.clientY - startY;
            let newX = origLeft + dx;
            let newY = origTop + dy;

            const wsRect = this.designer.workspace.getBoundingClientRect();
            const elRect = this.el.getBoundingClientRect();
            const maxX = wsRect.width - elRect.width;
            const maxY = wsRect.height - elRect.height;
            
            newX = Math.max(0, Math.min(newX, maxX));
            newY = Math.max(0, Math.min(newY, maxY));

            this.el.style.left = newX + 'px';
            this.el.style.top = newY + 'px';

            this.x = newX;
            this.y = newY;
            this.designer.redrawCablesForDevice(this.id);
        });

        window.addEventListener('mouseup', () => {
            if (this.isDragging) {
                this.isDragging = false;
                this.el.classList.remove('dragging');
                document.body.style.userSelect = '';
            }
        });
    }
}