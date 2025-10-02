class xmlRPCservice {
  async sendXmlRpcVia(method, params = []) {
    try {
      const response = await fetch('http://localhost:3000/xmlrpc', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ method, params })
      });

      // تحقق من حالة الاستجابة (مثلاً 500 Internal Server Error)
      if (!response.ok) {
        throw new Error(`HTTP error! status: ${response.status}`);
      }

      const data = await response.json();

      if (data.error) {
        console.error('XML-RPC Error:', data.error);
      } else {
        console.log('XML-RPC Result:', data.result);
      }
    } catch (err) {
      console.error('Fetch Error:', err.message);
      // هنا سيظهر الخطأ إذا كان:
      // - السيرفر معطّل (fetch failed)
      // - استجابة غير صالحة
      // - مشكلة في الشبكة
    }
  }
}

// === فئة رئيسية: NetworkDesigner ===
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
    this.devices = new Map(); // id → Device
    this.cables = new Map();  // id → Cable

    // الحالة
    this.deviceCounter = 0;
    this.cableCounter = 0;
    this.connectStartDeviceId = null;
    this.tempLine = null;
    this.selectedCableId = null;

    // SVG overlay
    this.svgNS = "http://www.w3.org/2000/svg";
    this.svg = this.createSvgOverlay();

    // أداة الكابل (تتبع الماوس)
    this.cableTool = this.createCableTool();

    // مُدير الملفات
    this.ioManager = new IOManager(this);
    //مدير XMLRPC
    this.RPCservice = new xmlRPCservice();
    this.init();
  }

  init() {
    this.setupEventListeners();
    this.setupDeviceLibrary();
    console.log('NetworkDesigner: Initialized');
  }
  updatePlaceholder() {
    if (this.devices.size === 0) {
      this.placeholder.style.opacity = '1';
      this.placeholder.style.pointerEvents = 'auto'; // قابل للتفاعل (اختياري)
    } else {
      this.placeholder.style.opacity = '0';
      this.placeholder.style.pointerEvents = 'none'; // ما يمنعش التفاعل مع العناصر تحته
    }
  }
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

  createCableTool() {
    const tool = document.createElement('div');
    tool.classList.add('cable-tool');
    document.body.appendChild(tool);
    return tool;
  }

  setupEventListeners() {
    window.addEventListener('resize', () => this.redrawAllCables());
    window.addEventListener('keydown', (e) => this.handleKeydown(e));
    window.addEventListener('mousemove', (e) => this.trackTempLine(e));
    window.addEventListener('mouseup', () => this.endDrag());

    this.workspace.addEventListener('click', () => this.clearSelection());
    this.workspace.addEventListener('dragover', (e) => e.preventDefault());
    this.workspace.addEventListener('drop', (e) => this.handleDrop(e));
  }

  handleKeydown(e) {
    if (e.key === 'Delete' && this.selectedCableId) {
      this.removeCable(this.selectedCableId);
    }
    if (e.key === 'Escape') {
      this.cancelConnection();
    }
  }

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

  handleDrop(e) {
    e.preventDefault();
    const type = e.dataTransfer.getData('text/plain') || 'pc';
    const rect = this.workspace.getBoundingClientRect();
    const x = e.clientX - rect.left - 32;
    const y = e.clientY - rect.top - 32;
    const snapped = this.snapToGrid(x, y);
    this.createDevice(type, snapped.x, snapped.y);
  }

  snapToGrid(x, y) {
    return {
      x: Math.round(x / this.GRID_SIZE) * this.GRID_SIZE,
      y: Math.round(y / this.GRID_SIZE) * this.GRID_SIZE
    };
  }

  getDeviceCenter(el) {
    const wsRect = this.workspace.getBoundingClientRect();
    const r = el.getBoundingClientRect();
    return {
      x: r.left - wsRect.left + r.width / 2,
      y: r.top - wsRect.top + r.height / 2
    };
  }

  async createDevice(type, x, y) {
    const tempId = `device-${++this.deviceCounter}`;
    const device = new Device(tempId, type, x, y, this);
    device.showLoading();
    try {
      const response = await this.RPCservice.sendXmlRpcVia("vm.clone", type);

      if (response.success) {
        // تحويل الـID المؤقت إلى الحقيقي
        device.updateDeviceId(response.id)
        device.hideLoading()
        this.devices.set(response.id, device);
        this.updatePlaceholder();
        console.log("نجحت العملية، ID:", response.id);
      } else {
        // حذف العنصر المؤقت إذا فشلت العملية
        this.deviceManager.removeDevice(tempId);
        console.error("فشلت العملية:", response.message);
        return tempId;
      }
    } catch (err) {
      this.deviceManager.removeDevice(tempId);
      console.error("حدث خطأ:", err);
    }

  }

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
  }

  createCableBetween(fromId, toId) {
    if (fromId === toId) return null;

    for (const [, c] of this.cables) {
      if ((c.from === fromId && c.to === toId) || (c.from === toId && c.to === fromId)) {
        return null;
      }
    }

    const id = `cable-${++this.cableCounter}`;
    const cable = new Cable(id, fromId, toId, this);
    this.cables.set(id, cable);
    this.selectCable(id);
    return cable;
  }

  removeCable(id) {
    const cable = this.cables.get(id);
    if (!cable) return;

    cable.remove();
    this.cables.delete(id);

    if (this.selectedCableId === id) {
      this.selectedCableId = null;
    }
  }

  selectCable(id) {
    if (this.selectedCableId && this.cables.has(this.selectedCableId)) {
      const prev = this.cables.get(this.selectedCableId);
      prev.deselect();
    }
    this.selectedCableId = id;
    const cable = this.cables.get(id);
    if (cable) cable.select();
  }

  clearSelection() {
    if (this.selectedCableId) {
      const cable = this.cables.get(this.selectedCableId);
      if (cable) cable.deselect();
      this.selectedCableId = null;
    }
  }

  startConnection(deviceId) {
    if (this.connectStartDeviceId) return;

    this.connectStartDeviceId = deviceId;

    this.tempLine = document.createElementNS(this.svgNS, 'line');
    this.tempLine.setAttribute('stroke-width', 3);
    this.tempLine.setAttribute('stroke-dasharray', '6 6');
    this.tempLine.setAttribute('class', 'connection-line temp');
    this.tempLine.style.pointerEvents = 'none';
    this.svg.appendChild(this.tempLine);
  }

  finishConnection(toId) {
    if (this.connectStartDeviceId && this.connectStartDeviceId !== toId) {
      this.createCableBetween(this.connectStartDeviceId, toId);
    }
    this.cancelConnection();
  }

  cancelConnection() {
    if (this.tempLine && this.tempLine.parentNode) {
      this.tempLine.parentNode.removeChild(this.tempLine);
    }
    this.tempLine = null;
    this.connectStartDeviceId = null;
    this.cableTool.classList.remove('active');
  }

  trackTempLine(e) {
    if (!this.tempLine || !this.connectStartDeviceId) return;

    const startDev = this.devices.get(this.connectStartDeviceId);
    const startCenter = this.getDeviceCenter(startDev.el);
    const wsRect = this.workspace.getBoundingClientRect();
    const mx = e.clientX - wsRect.left;
    const my = e.clientY - wsRect.top;

    this.tempLine.setAttribute('x1', startCenter.x);
    this.tempLine.setAttribute('y1', startCenter.y);
    this.tempLine.setAttribute('x2', mx);
    this.tempLine.setAttribute('y2', my);

    this.cableTool.style.left = (e.pageX - 12) + 'px';
    this.cableTool.style.top = (e.pageY - 12) + 'px';
    this.cableTool.classList.add('active');
  }

  redrawAllCables() {
    for (const [, cable] of this.cables) {
      cable.position();
    }
  }

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

  redrawCablesForDevice(deviceId) {
    for (const [, cable] of this.cables) {
      if (cable.from === deviceId || cable.to === deviceId) {
        cable.position();
      }
    }
  }
}



class DeviceFactoty {

}
// === فئة: Device ===
class Device {
  constructor(id, type, x, y, designer) {
    this.type = type;
    this.x = x;
    this.y = y;
    this.designer = designer;
    this.isDragging = false;
    this.id = id;
    this.el = this.createElement(x, y);
    this.designer.workspace.appendChild(this.el);
    this.enableDrag();
    this.addEventListeners();
    this.createLabel();

  }
  createOSBadge() {
    // تحديد نظام التشغيل بناءً على نوع الجهاز
    const osInfo = this.getOSInfo();

    this.badge = document.createElement('div');
    this.badge.className = 'device-os-badge';
    this.badge.innerHTML = `
      <span class="os-name">${osInfo.name}</span>
      <span class="os-icon">${osInfo.icon}</span>
    `;

    Object.assign(this.badge.style, {
      position: 'absolute',
      top: '-20px',
      left: '50%',
      transform: 'translateX(-50%)',
      background: osInfo.color,
      color: 'white',
      padding: '2px 8px',
      borderRadius: '10px',
      fontSize: '10px',
      fontWeight: 'bold',
      display: 'flex',
      alignItems: 'center',
      gap: '4px',
      whiteSpace: 'nowrap',
      zIndex: 20,
      boxShadow: '0 2px 5px rgba(0,0,0,0.2)'
    });

    this.el.appendChild(this.badge);
  }

  getOSInfo() {
    // يمكنك تخصيص هذا بناءً على احتياجاتك
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
  updateDeviceId(id) {
    this.id = id;
  }
  createLabel() {
    this.label = document.createElement('div');
    this.label.className = 'device-label';
    this.label.textContent = this.generateName(); // مثل: Router 1
    this.el.appendChild(this.label);
  }

  generateName() {
    const names = {
      router: 'router',
      switch: 'switch',
      pc: 'pc',
      server: 'server',
      laptop: 'laptop',
      tablet: 'tablet',
      wireless: 'wireless',
    };
    const baseName = names[this.type] || 'جهاز';
    return `${baseName}`;
  }

  setupInfoPanel() {
    this.infoPanel = document.createElement('div');
    this.infoPanel.className = 'device-info-panel';

    this.infoPanel.innerHTML = `
    <div class="device-info-header">${this.generateName()}</div>
    <div class="device-info-actions">
      <div class="device-info-btn cable" title="إضافة كابل">
        <i class="fas fa-plug"></i>
        <span>كابل</span>
      </div>
      <div class="device-info-btn power-on" title="تشغيل الجهاز">
        <i class="fas fa-play"></i>
        <span>تشغيل</span>
      </div>
      <div class="device-info-btn power-off" title="إيقاف الجهاز">
        <i class="fas fa-stop"></i>
        <span>إيقاف</span>
      </div>
      <div class="device-info-btn vnc" title="فتح واجهة VNC">
        <i class="fas fa-desktop"></i>
        <span>VNC</span>
      </div>
    </div>
  `;

    document.body.appendChild(this.infoPanel);
    this.attachPanelEvents();
  }

  createElement(x, y) {
    const el = document.createElement('div');
    el.className = 'device-icon animated';
    el.dataset.id = this.id;
    el.dataset.type = this.type;

    Object.assign(el.style, {
      position: 'absolute',
      left: x + 'px',
      top: y + 'px',
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

    const i = document.createElement('i');
    i.style.fontSize = '26px';
    i.style.opacity = '1';
    i.style.transition = 'opacity 0.3s ease';

    const iconMap = {
      router: 'fas fa-wifi router-icon',
      switch: 'fas fa-network-wired switch-icon',
      pc: 'fas fa-desktop pc-icon',
      server: 'fas fa-server server-icon',
      laptop: 'fas fa-laptop laptop-icon',
      tablet: 'fas fa-tablet-alt pc-icon',
      wireless: 'fas fa-wifi wireless-icon',
      cable: 'fas fa-plug cable-icon',
      terminal: 'icons/terminal.png'
    };

    const iconValue = iconMap[this.type];
    if (iconValue && iconValue.startsWith('fas')) {
      // أيقونة Font Awesome
      i.className = iconValue;
    } else if (iconValue) {
      // صورة من ملف
      const existingIcon = this.el.querySelector('i');
      if (existingIcon) {
        this.el.removeChild(existingIcon);
      }
      // إنشاء عنصر صورة
      const img = document.createElement('img');
      const iconPath = `icons/${this.type}.png`; // أو .svg

      img.src = iconPath;
      img.alt = `${this.type} icon`;
      img.style.width = '100%';
      img.style.height = '100%';
      img.style.objectFit = 'contain';
      img.style.opacity = '0'; // نبدأ بالشفافية

      // إضافة الصورة إلى العنصر
      this.el.appendChild(img);
    } else {
      // أيقونة افتراضية
      i.className = 'fas fa-question';
    }
    el.appendChild(i);
    return el;
  }
  // دالة لعرض علامة التحميل
  showLoading() {
    const loadingDiv = document.createElement('div');
    loadingDiv.className = 'device-loading';
    Object.assign(loadingDiv.style, {
      position: 'absolute',
      top: '0',
      left: '0',
      width: '100%',
      height: '100%',
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'center',
      background: 'rgba(255, 255, 255, 0.5)',
      zIndex: '2'
    });

    const spinner = document.createElement('div');
    spinner.className = 'device-loading-spinner';
    loadingDiv.appendChild(spinner);

    this.el.appendChild(loadingDiv);
  }

  // دالة لإخفاء علامة التحميل
  hideLoading() {
    const loading = this.el.querySelector('.device-loading');
    const icon = this.el.querySelector('i');

    if (loading) loading.remove();
    if (icon) icon.style.opacity = '1';
  }
  CreateDevice() { // استقبل الأيقونة
    const loadingDiv = document.createElement('div');
    loadingDiv.className = 'device-loading';
    Object.assign(loadingDiv.style, {
      position: 'absolute',
      top: '0',
      left: '0',
      width: '100%',
      height: '100%',
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'center',
      background: 'rgba(255, 255, 255, 0.5)',
      zIndex: '2'
    });

    const spinner = document.createElement('div');
    spinner.className = 'device-loading-spinner';
    loadingDiv.appendChild(spinner);
    this.el.appendChild(loadingDiv);

    const willFail = Math.random() < 0.3;
    const creationTime = 1500 + Math.random() * 1000;

    setTimeout(() => {
      if (willFail) {
        this.handleCreationFailure(el, loadingDiv);
      } else {
        this.handleCreationSuccess(el, loadingDiv, icon);
      }
    }, creationTime);

    return el;
  }


  handleCreationFailure(el, loadingDiv) {
    loadingDiv.remove();
    const failureDiv = document.createElement('div');
    failureDiv.className = 'device-failure';
    failureDiv.innerHTML = '✕';
    el.appendChild(failureDiv);
    el.style.animation = 'shake 0.5s ease-in-out';

    setTimeout(() => {
      el.classList.add('fade-out');
      setTimeout(() => this.designer.removeDevice(this.id), 500);
    }, 1500);
  }

  handleCreationSuccess(el, loadingDiv, icon) {
    loadingDiv.style.transition = 'opacity 0.3s ease';
    loadingDiv.style.opacity = '0';
    setTimeout(() => {
      loadingDiv.remove();
      icon.style.opacity = '1';
      el.classList.add('device-pop-in');
      setTimeout(() => el.classList.remove('device-pop-in'), 300);
    }, 300);

    requestAnimationFrame(() => {
      el.style.transition = 'transform .18s ease, opacity .25s ease';
      el.style.transform = 'translateY(0px)';
      el.style.opacity = '1';
    });
  }

  addEventListeners() {
    this.el.addEventListener('click', (e) => {
      if (e.shiftKey) {
        e.stopPropagation();
        this.onShiftClick();
      } else {
        this.designer.selectDevice(this.id);
      }
    });
  }

  onShiftClick() {
    if (!this.designer.connectStartDeviceId) {
      this.designer.startConnection(this.id);
    } else if (this.designer.connectStartDeviceId === this.id) {
      this.designer.cancelConnection();
    } else {
      this.designer.finishConnection(this.id);
    }
  }

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
  }
}

// === فئة: Cable ===
class Cable {
  constructor(id, from, to, designer) {
    this.id = id;
    this.from = from;
    this.to = to;
    this.designer = designer;

    this.lineEl = this.createLine();
    this.midEl = this.createMidPoint();
    this.designer.svg.appendChild(this.lineEl);
    this.designer.workspace.appendChild(this.midEl);

    this.position();
  }

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
      if (confirm('هل تريد حذف هذا الكابل؟')) {
        this.designer.removeCable(this.id);
      }
    });

    return line;
  }

  createMidPoint() {
    const mid = document.createElement('div');
    mid.className = 'cable-delete';
    Object.assign(mid.style, {
      position: 'absolute',
      width: '16px',
      height: '16px',
      borderRadius: '50%',
      background: '#e74a3b',
      color: 'white',
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'center',
      cursor: 'pointer',
      zIndex: 5,
      fontSize: '12px',
      pointerEvents: 'auto'
    });
    mid.textContent = '×';
    mid.dataset.cableId = this.id;

    mid.addEventListener('click', (e) => {
      e.stopPropagation();
      this.designer.removeCable(this.id);
    });

    mid.addEventListener('contextmenu', (e) => {
      e.preventDefault();
      if (confirm('هل تريد حذف هذا الكابل؟')) {
        this.designer.removeCable(this.id);
      }
    });

    return mid;
  }

  position() {
    const fromDev = this.designer.devices.get(this.from);
    const toDev = this.designer.devices.get(this.to);
    if (!fromDev || !toDev) return;

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

  select() {
    this.lineEl.style.filter = 'drop-shadow(0px 0px 6px rgba(78,115,223,0.6))';
    this.midEl.style.transform = 'scale(1.15)';
  }

  deselect() {
    this.lineEl.style.filter = '';
    this.midEl.style.transform = 'scale(1)';
  }

  remove() {
    if (this.lineEl.parentNode) this.lineEl.parentNode.removeChild(this.lineEl);
    if (this.midEl.parentNode) this.midEl.parentNode.removeChild(this.midEl);
  }
}

// === فئة: IOManager (للحفظ/التحميل) ===
class IOManager {
  constructor(designer) {
    this.designer = designer;
    this.setupIOButtons();
  }

  setupIOButtons() {
    if (this.designer.saveBtn) {
      this.designer.saveBtn.addEventListener('click', () => this.exportProject());
    }
    if (this.designer.loadBtn && this.designer.loadFileInput) {
      this.designer.loadBtn.addEventListener('click', () => this.designer.loadFileInput.click());
      this.designer.loadFileInput.addEventListener('change', (e) => this.handleFileLoad(e));
    }
  }

  exportProject() {
    const data = {
      devices: Array.from(this.designer.devices.values()).map(d => ({ id: d.id, type: d.type, x: d.x, y: d.y })),
      cables: Array.from(this.designer.cables.values()).map(c => ({ id: c.id, from: c.from, to: c.to }))
    };
    const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = 'cyperland-project.json';
    a.click();
    URL.revokeObjectURL(url);
  }

  handleFileLoad(e) {
    const file = e.target.files[0];
    if (!file) return;
    const reader = new FileReader();
    reader.onload = () => this.importProject(reader.result);
    reader.readAsText(file);
  }

  importProject(jsonStr) {
    try {
      const obj = JSON.parse(jsonStr);

      // مسح الحالي
      for (const id of Array.from(this.designer.cables.keys())) this.designer.removeCable(id);
      for (const id of Array.from(this.designer.devices.keys())) this.designer.removeDevice(id);

      // إعادة الأجهزة
      (obj.devices || []).forEach(d => {
        const numeric = parseInt(d.id.split('-')[1] || '0');
        if (!isNaN(numeric) && numeric > this.designer.deviceCounter) {
          this.designer.deviceCounter = numeric;
        }
        this.designer.createDevice(d.type, d.x, d.y);
      });

      // إعادة الكابلات
      setTimeout(() => {
        (obj.cables || []).forEach(c => {
          const from = Array.from(this.designer.devices.keys())[0];
          const to = Array.from(this.designer.devices.keys())[1];
          if (from && to) this.designer.createCableBetween(from, to);
        });
      }, 200);
    } catch (err) {
      alert('خطأ في استيراد المشروع: ' + err.message);
    }
  }
}

// === التهيئة عند التحميل ===
document.addEventListener('DOMContentLoaded', () => {
  new NetworkDesigner();
});
document.getElementById('cpu').innerText=10;