/**
 * Application Initialization
 * تهيئة التطبيق الرئيسي
 */

// تهيئة التطبيق عند تحميل الصفحة
document.addEventListener('DOMContentLoaded', function() {
    try {
        // إنشاء مثيل المصمم الرئيسي
        const networkDesigner = new NetworkDesigner();
        
        // جعل المصمم متاحاً globally للتصحيح
        window.networkDesigner = networkDesigner;
        
        console.log('✅ Application initialized successfully');
        
        // إظهار رسالة ترحيب
        setTimeout(() => {
            showWelcomeMessage();
        }, 1000);
        
    } catch (error) {
        console.error('❌ Failed to initialize application:', error);
        showErrorMessage('فشل في تهيئة التطبيق: ' + error.message);
    }
});

/**
 * عرض رسالة ترحيب
 */
function showWelcomeMessage() {
    const notification = document.createElement('div');
    notification.className = 'app-notification alert alert-info alert-dismissible fade show';
    notification.innerHTML = `
        <strong>مرحباً بك في محاكي الشبكة الاحترافي!</strong>
        <br>طريقة الاستخدام:
        <ul class="mb-2 mt-2">
            <li>اسحب الأجهزة من القائمة الجانبية</li>
            <li>للربط: <kbd>Shift + Click</kbd> على الجهاز الأول ثم الثاني</li>
            <li>لإلغاء الربط: <kbd>ESC</kbd> أو النقر على مساحة فارغة</li>
            <li>لحذف كابل: النقر على نقطة المنتصف أو <kbd>Delete</kbd></li>
        </ul>
        <button type="button" class="btn-close" data-bs-dismiss="alert"></button>
    `;
    
    Object.assign(notification.style, {
        position: 'fixed',
        top: '20px',
        right: '20px',
        zIndex: 1060,
        minWidth: '450px',
        maxWidth: '500px'
    });
    
    document.body.appendChild(notification);
    
    setTimeout(() => {
        if (notification.parentNode) {
            notification.remove();
        }
    }, 10000);
}

/**
 * عرض رسالة خطأ
 */
function showErrorMessage(message) {
    const errorDiv = document.createElement('div');
    errorDiv.className = 'alert alert-danger m-3';
    errorDiv.innerHTML = `
        <h4>خطأ في التطبيق</h4>
        <p>${message}</p>
        <button onclick="location.reload()" class="btn btn-warning btn-sm">إعادة تحميل الصفحة</button>
    `;
    
    document.querySelector('.container-fluid').prepend(errorDiv);
}

/**
 * وظائف مساعدة global
 */
window.AppHelpers = {
    /**
     * تصدير حالة التطبيق للتdebug
     */
    exportState: function() {
        if (!window.networkDesigner) {
            console.error('NetworkDesigner not initialized');
            return;
        }
        
        const state = {
            devices: Array.from(window.networkDesigner.devices.entries()),
            cables: Array.from(window.networkDesigner.cables.entries()),
            deviceCount: window.networkDesigner.devices.size,
            cableCount: window.networkDesigner.cables.size,
            zoomLevel: window.networkDesigner.zoomLevel
        };
        
        console.log('Application State:', state);
        return state;
    },
    
    /**
     * مسح وحدة التحكم
     */
    clearConsole: function() {
        console.clear();
        console.log('🚀 Network Simulator Console Cleared');
    },
    
    /**
     * إضافة جهاز اختبار
     */
    addTestDevices: function() {
        if (!window.networkDesigner) return;
        
        const types = ['router', 'switch', 'pc', 'server', 'laptop'];
        types.forEach((type, index) => {
            setTimeout(() => {
                window.networkDesigner.createDevice(type, 100 + index * 100, 100 + index * 80);
            }, index * 500);
        });
    }
};

// إضافة أنماط إضافية للقوائم المنبثقة
const additionalStyles = `
    .device-context-menu .context-item,
    .context-menu .context-item {
        padding: 8px 12px;
        cursor: pointer;
        border-bottom: 1px solid #f0f0f0;
        transition: background-color 0.2s;
        display: flex;
        justify-content: space-between;
        align-items: center;
    }
    
    .device-context-menu .context-item:last-child,
    .context-menu .context-item:last-child {
        border-bottom: none;
    }
    
    .device-context-menu .context-item:hover,
    .context-menu .context-item:hover {
        background-color: #f8f9fa;
    }
    
    .device-context-menu .context-item[data-action="delete"]:hover,
    .context-menu .context-item[data-action="delete"]:hover {
        background-color: #fee;
        color: #dc3545;
    }
    
    .app-notification {
        animation: slideInRight 0.3s ease-out;
    }
    
    @keyframes slideInRight {
        from {
            transform: translateX(100%);
            opacity: 0;
        }
        to {
            transform: translateX(0);
            opacity: 1;
        }
    }

    /* أنماط وضع الاتصال */
    .connecting-mode {
        cursor: crosshair !important;
    }

    .connecting-mode .device-icon {
        cursor: crosshair;
    }

    .connecting-mode .device-icon:not(.connecting-source):hover {
        border-color: var(--success-color) !important;
        box-shadow: 0 0 0 3px rgba(76, 201, 240, 0.3) !important;
    }

    /* تأثيرات الجهاز المصدر والهدف */
    .device-icon.connecting-source {
        animation: pulse-connection 2s infinite;
        border-color: var(--primary-color) !important;
        box-shadow: 0 0 0 3px rgba(67, 97, 238, 0.5) !important;
    }

    .device-icon.connection-target {
        border-color: var(--success-color) !important;
        box-shadow: 0 0 0 3px rgba(76, 201, 240, 0.5) !important;
        transform: scale(1.05);
    }

    @keyframes pulse-connection {
        0%, 100% { 
            box-shadow: 0 0 0 3px rgba(67, 97, 238, 0.5);
        }
        50% { 
            box-shadow: 0 0 0 6px rgba(67, 97, 238, 0.3);
        }
    }

    /* أداة الكابل المتحركة */
    .cable-tool {
        position: fixed;
        width: 32px;
        height: 32px;
        background: var(--primary-color);
        color: white;
        border-radius: 50%;
        display: flex;
        align-items: center;
        justify-content: center;
        z-index: 1000;
        pointer-events: none;
        opacity: 0;
        transform: scale(0.8);
        transition: all 0.3s ease;
        box-shadow: 0 4px 12px rgba(0,0,0,0.2);
    }

    .cable-tool.active {
        opacity: 1;
        transform: scale(1);
    }

    .cable-tool i {
        font-size: 16px;
    }

    /* الخط المؤقت المتحسن */
    .connection-line.temp {
        stroke: var(--primary-color);
        stroke-width: 3;
        stroke-dasharray: 6 6;
        animation: dash-move 1s linear infinite;
        opacity: 0.8;
    }

    @keyframes dash-move {
        to {
            stroke-dashoffset: -12;
        }
    }
`;

// إضافة الأنماط إلى head المستند
const styleSheet = document.createElement('style');
styleSheet.textContent = additionalStyles;
document.head.appendChild(styleSheet);

console.log('🎯 Network Simulator App Loaded Successfully');