// === فئة: UploadManager ===
class UploadManager {
  constructor(designer) {
    this.designer = designer;
    this.uploadArea = designer.uploadArea;
    this.fileInput = designer.fileInput;
    this.uploadProgress = designer.uploadProgress;
    this.progressBar = designer.progressBar;
    this.uploadStatus = designer.uploadStatus;
    this.toggleUploadBtn = designer.toggleUploadBtn;
  }

  init() {
    this.setupEventListeners();
  }

  setupEventListeners() {
    // فتح/إغلاق قسم الرفع
    this.toggleUploadBtn.addEventListener('click', () => {
      this.uploadArea.style.display = 
        this.uploadArea.style.display === 'none' ? 'block' : 'none';
    });

    // النقر للرفع
    this.uploadArea.addEventListener('click', () => {
      this.fileInput.click();
    });

    // تغيير الملفات
    this.fileInput.addEventListener('change', (e) => {
      this.handleFiles(e.target.files);
    });

    // السحب والإفلات
    this.uploadArea.addEventListener('dragover', (e) => {
      e.preventDefault();
      this.uploadArea.classList.add('drag-over');
    });

    this.uploadArea.addEventListener('dragleave', (e) => {
      e.preventDefault();
      if (!this.uploadArea.contains(e.relatedTarget)) {
        this.uploadArea.classList.remove('drag-over');
      }
    });

    this.uploadArea.addEventListener('drop', (e) => {
      e.preventDefault();
      this.uploadArea.classList.remove('drag-over');
      this.handleFiles(e.dataTransfer.files);
    });
  }

  async handleFiles(files) {
    if (files.length === 0) return;

    this.showProgress();
    
    for (let file of files) {
      await this.processFile(file);
    }
    
    this.hideProgress();
  }

  async processFile(file) {
    try {
      this.updateProgress(0, `جاري معالجة ${file.name}...`);
      
      // محاكاة التقدم
      for (let i = 0; i <= 100; i += 10) {
        await this.delay(100);
        this.updateProgress(i, `جاري رفع ${file.name}... (${i}%)`);
      }

      // معالجة الملف حسب النوع
      const fileType = file.type;
      const fileName = file.name.toLowerCase();

      if (fileName.endsWith('.json')) {
        await this.handleJsonFile(file);
      } else if (fileName.endsWith('.xml')) {
        await this.handleXmlFile(file);
      } else if (fileType.startsWith('image/')) {
        await this.handleImageFile(file);
      } else {
        await this.handleOtherFile(file);
      }

      this.updateProgress(100, `تم رفع ${file.name} بنجاح`);
      await this.delay(1000);

    } catch (error) {
      this.updateProgress(0, `خطأ في رفع ${file.name}: ${error.message}`);
      console.error('Upload error:', error);
    }
  }

  async handleJsonFile(file) {
    const text = await this.readFileAsText(file);
    const data = JSON.parse(text);
    
    // معالجة بيانات JSON
    if (data.devices && data.cables) {
      this.designer.ioManager.importProject(text);
    } else {
      console.log('JSON data loaded:', data);
      // معالجة إضافية للبيانات
    }
  }

  async handleXmlFile(file) {
    const text = await this.readFileAsText(file);
    console.log('XML content:', text);
    // معالجة XML
  }

  async handleImageFile(file) {
    const url = URL.createObjectURL(file);
    console.log('Image URL:', url);
    // معالجة الصور
  }

  async handleOtherFile(file) {
    console.log('Other file type:', file.type, file.name);
    // معالجة أنواع الملفات الأخرى
  }

  readFileAsText(file) {
    return new Promise((resolve, reject) => {
      const reader = new FileReader();
      reader.onload = e => resolve(e.target.result);
      reader.onerror = e => reject(e);
      reader.readAsText(file);
    });
  }

  showProgress() {
    this.uploadProgress.style.display = 'block';
    this.updateProgress(0, 'جاري التحضير...');
  }

  hideProgress() {
    setTimeout(() => {
      this.uploadProgress.style.display = 'none';
      this.updateProgress(0, 'جاري الرفع...');
    }, 2000);
  }

  updateProgress(percent, status) {
    this.progressBar.style.width = percent + '%';
    this.uploadStatus.textContent = status;
    
    // تحديث ألوان شريط التقدم
    if (percent < 50) {
      this.progressBar.style.background = 'linear-gradient(90deg, var(--primary-color), var(--primary-light))';
    } else if (percent < 80) {
      this.progressBar.style.background = 'linear-gradient(90deg, var(--warning-color), #ffb347)';
    } else {
      this.progressBar.style.background = 'linear-gradient(90deg, var(--success-color), #4cc9f0)';
    }
  }

  delay(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
  }
}

// === باقي الفئات تبقى كما هي من كودك الأصلي ===
class xmlRPCservice {
  async sendXmlRpcVia(method, params = []) {
    try {
      const response = await fetch('http://localhost:3000/xmlrpc', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ method, params })
      });

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
    }
  }
}

// Device, Cable, IOManager classes remain the same as your original code...

// === التهيئة عند التحميل ===
document.addEventListener('DOMContentLoaded', () => {
  new NetworkDesigner();
});