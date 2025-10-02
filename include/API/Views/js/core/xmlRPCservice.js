/**
 * XmlRPCService - خدمة اتصال XML-RPC
 * إدارة الاتصالات مع الخادم الخلفي
 */
class XmlRPCService {
    constructor() {
        this.baseUrl = 'http://localhost:3000/xmlrpc';
        this.timeout = 10000;
    }

    /**
     * إرسال طلب XML-RPC
     */
    async sendXmlRpc(method, params = []) {
        try {
            const controller = new AbortController();
            const timeoutId = setTimeout(() => controller.abort(), this.timeout);

            const response = await fetch(this.baseUrl, {
                method: 'POST',
                headers: { 
                    'Content-Type': 'application/json',
                    'Accept': 'application/json'
                },
                body: JSON.stringify({ 
                    method, 
                    params,
                    timestamp: new Date().toISOString()
                }),
                signal: controller.signal
            });

            clearTimeout(timeoutId);

            if (!response.ok) {
                throw new Error(`HTTP error! status: ${response.status}`);
            }

            const data = await response.json();

            if (data.error) {
                console.error('XML-RPC Error:', data.error);
                throw new Error(data.error);
            }

            console.log('XML-RPC Success:', data.result);
            return data.result;

        } catch (error) {
            console.error('XML-RPC Fetch Error:', error);
            
            if (error.name === 'AbortError') {
                throw new Error('انتهت مهلة الاتصال بالخادم');
            }
            
            throw error;
        }
    }

    /**
     * استنساخ جهاز ظاهري
     */
    async cloneVirtualMachine(deviceType) {
        try {
            const result = await this.sendXmlRpc('vm.clone', [deviceType]);
            return {
                success: true,
                id: result.id || `vm-${Date.now()}`,
                message: 'تم استنساخ الجهاز الظاهري بنجاح'
            };
        } catch (error) {
            return {
                success: false,
                error: error.message,
                message: 'فشل في استنساخ الجهاز الظاهري'
            };
        }
    }

    /**
     * تشغيل جهاز ظاهري
     */
    async startVirtualMachine(vmId) {
        try {
            const result = await this.sendXmlRpc('vm.start', [vmId]);
            return {
                success: true,
                message: 'تم تشغيل الجهاز الظاهري بنجاح'
            };
        } catch (error) {
            return {
                success: false,
                error: error.message,
                message: 'فشل في تشغيل الجهاز الظاهري'
            };
        }
    }

    /**
     * إيقاف جهاز ظاهري
     */
    async stopVirtualMachine(vmId) {
        try {
            const result = await this.sendXmlRpc('vm.stop', [vmId]);
            return {
                success: true,
                message: 'تم إيقاف الجهاز الظاهري بنجاح'
            };
        } catch (error) {
            return {
                success: false,
                error: error.message,
                message: 'فشل في إيقاف الجهاز الظاهري'
            };
        }
    }

    /**
     * الحصول على حالة الجهاز الظاهري
     */
    async getVirtualMachineStatus(vmId) {
        try {
            const result = await this.sendXmlRpc('vm.status', [vmId]);
            return {
                success: true,
                status: result.status || 'unknown',
                message: 'تم الحصول على حالة الجهاز'
            };
        } catch (error) {
            return {
                success: false,
                error: error.message,
                message: 'فشل في الحصول على حالة الجهاز'
            };
        }
    }

    /**
     * فتح اتصال VNC
     */
    async openVNCConnection(vmId) {
        try {
            const result = await this.sendXmlRpc('vm.vnc', [vmId]);
            return {
                success: true,
                url: result.url,
                message: 'تم فتح اتصال VNC'
            };
        } catch (error) {
            return {
                success: false,
                error: error.message,
                message: 'فشل في فتح اتصال VNC'
            };
        }
    }

    /**
     * التحقق من اتصال الخادم
     */
    async checkServerConnection() {
        try {
            const result = await this.sendXmlRpc('system.ping');
            return {
                success: true,
                message: 'الخادم متصل ويعمل',
                timestamp: result.timestamp
            };
        } catch (error) {
            return {
                success: false,
                error: error.message,
                message: 'لا يمكن الاتصال بالخادم'
            };
        }
    }

    /**
     * الحصول على إحصائيات النظام
     */
    async getSystemStats() {
        try {
            const result = await this.sendXmlRpc('system.stats');
            return {
                success: true,
                stats: result
            };
        } catch (error) {
            return {
                success: false,
                error: error.message,
                stats: this.getFallbackStats()
            };
        }
    }

    /**
     * إحصائيات احتياطية في حالة فشل الاتصال
     */
    getFallbackStats() {
        return {
            cpu: Math.floor(Math.random() * 80) + 10,
            memory: Math.floor(Math.random() * 70) + 15,
            disk: Math.floor(Math.random() * 60) + 20,
            network: Math.floor(Math.random() * 100),
            timestamp: new Date().toISOString()
        };
    }
}