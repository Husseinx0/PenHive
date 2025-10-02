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

  createDevice(type, x, y) {
    const id = `device-${++this.deviceCounter}`;
    const device = new Device(id, type, x, y, this);
    this.devices.set(id, device);
    this.updatePlaceholder();

    return id;
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
