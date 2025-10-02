/**
 * Cable - فئة تمثل كابل اتصال بين جهازين
 * إدارة إنشاء وعرض وسلوك الكابلات
 */
class Cable {
    constructor(id, from, to, designer) {
        this.id = id;
        this.from = from;
        this.to = to;
        this.designer = designer;

        if (!this.validateDevices()) {
            console.warn(`Cable ${id}: Invalid devices - from: ${from}, to: ${to}`);
            return;
        }

        this.lineEl = this.createLine();
        this.midEl = this.createMidPoint();
        
        this.designer.svg.appendChild(this.lineEl);
        this.designer.workspace.appendChild(this.midEl);

        this.position();
    }

    /**
     * التحقق من صحة الأجهزة المرتبطة
     */
    validateDevices() {
        const fromExists = this.designer.devices.has(this.from);
        const toExists = this.designer.devices.has(this.to);
        
        if (!fromExists || !toExists) {
            console.error(`One or both devices not found: ${this.from}, ${this.to}`);
            return false;
        }
        
        return true;
    }

    /**
     * التحقق مما إذا كان الكابل صالحاً
     */
    isValid() {
        return this.validateDevices() && 
               this.lineEl && this.lineEl.parentNode &&
               this.midEl && this.midEl.parentNode;
    }

    /**
     * إنشاء خط الاتصال
     */
    createLine() {
        const line = document.createElementNS(this.designer.svgNS, 'line');
        line.setAttribute('stroke-width', 4);
        line.setAttribute('stroke-linecap', 'round');
        line.setAttribute('class', 'connection-line');
        line.style.pointerEvents = 'auto';
        line.dataset.id = this.id;

        line.addEventListener('click', (e) => {
            e.stopPropagation();
            this.designer.selectCable(this.id);
        });

        line.addEventListener('contextmenu', (e) => {
            e.preventDefault();
            this.showContextMenu(e);
        });

        return line;
    }

    /**
     * إنشاء نقطة المنتصف للحذف
     */
    createMidPoint() {
        const mid = document.createElement('div');
        mid.className = 'cable-delete';
        mid.textContent = '×';
        mid.dataset.cableId = this.id;

        mid.addEventListener('click', (e) => {
            e.stopPropagation();
            this.designer.removeCable(this.id);
        });

        return mid;
    }

    /**
     * تحديث موضع الكابل
     */
    position() {
        const fromDev = this.designer.devices.get(this.from);
        const toDev = this.designer.devices.get(this.to);
        
        if (!fromDev || !toDev) {
            this.remove();
            return;
        }

        const fromCenter = this.designer.getDeviceCenter(fromDev.el);
        const toCenter = this.designer.getDeviceCenter(toDev.el);

        this.lineEl.setAttribute('x1', fromCenter.x);
        this.lineEl.setAttribute('y1', fromCenter.y);
        this.lineEl.setAttribute('x2', toCenter.x);
        this.lineEl.setAttribute('y2', toCenter.y);

        const mx = (fromCenter.x + toCenter.x) / 2;
        const my = (fromCenter.y + toCenter.y) / 2;
        this.midEl.style.left = (mx - 8) + 'px';
        this.midEl.style.top = (my - 8) + 'px';
    }

    /**
     * تحديد الكابل
     */
    select() {
        this.lineEl.classList.add('selected');
        this.midEl.style.transform = 'scale(1.15)';
    }

    /**
     * إلغاء تحديد الكابل
     */
    deselect() {
        this.lineEl.classList.remove('selected');
        this.midEl.style.transform = 'scale(1)';
    }

    /**
     * إزالة الكابل
     */
    remove() {
        if (this.lineEl.parentNode) {
            this.lineEl.parentNode.removeChild(this.lineEl);
        }
        if (this.midEl.parentNode) {
            this.midEl.parentNode.removeChild(this.midEl);
        }
    }

    /**
     * عرض قائمة السياق
     */
    showContextMenu(e) {
        const menu = document.createElement('div');
        menu.className = 'context-menu';
        menu.innerHTML = `
            <div class="context-item" data-action="delete">حذف الكابل</div>
            <div class="context-item" data-action="properties">خصائص الاتصال</div>
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
            case 'delete':
                this.designer.removeCable(this.id);
                break;
            case 'properties':
                this.showProperties();
                break;
        }
    }

    /**
     * عرض خصائص الاتصال
     */
    showProperties() {
        const fromDev = this.designer.devices.get(this.from);
        const toDev = this.designer.devices.get(this.to);
        
        if (!fromDev || !toDev) return;

        const propertiesContent = document.getElementById('device-properties-content');
        propertiesContent.innerHTML = `
            <div class="properties-header mb-3">
                <h6 class="fw-bold">اتصال شبكي</h6>
                <small class="text-muted">${this.id}</small>
            </div>
            <div class="properties-list">
                <div class="property-item mb-2">
                    <strong>من:</strong> ${fromDev.generateName()}
                </div>
                <div class="property-item mb-2">
                    <strong>إلى:</strong> ${toDev.generateName()}
                </div>
                <div class="property-item mb-2">
                    <strong>الحالة:</strong> <span class="text-success">● متصل</span>
                </div>
                <div class="property-item">
                    <strong>النوع:</strong> كابل إيثرنت
                </div>
            </div>
        `;
    }
}