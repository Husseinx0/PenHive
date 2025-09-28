#pragma once 

#include "Core/interfaces/IDatabase.hpp"
#include "Utils/Result.hpp"
#include <uuid/uuid.h>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/asio.hpp>
#include <string>
#include <map>
class VirtualMachinePool : public IRocksDB
{
public:
    VirtualMachinePool(/* args */);
    ~VirtualMachinePool();
    /** 
     * Function to allocate a new VM object
     * 
     * 
     * 
     * @return oid on success, -1 error inserting in DB or -2 error parsing
     *  the template
    */
    [[nodiscard]] Result<int> allocate(
    );
    void get(
        int vID
    ) {
        //return 
    }
    void set(
    ){
        std::map<std::string,char
        std::string name;
        name.find('c');
    };
private:
    [[nodiscard]] std::string generate_uuid(){
        using  namespace boost;
        uuids::random_generator gen;
        uuids::uuid u = gen();
        return  uuids::to_string(u);  
    }
    [[nodiscard]] int  findAvailablePort(
        int startPort = 5900, 
        int endPort = 6000){
        if (startPort < 1 || endPort > 65535 || startPort > endPort) {
            throw std::invalid_argument("Invalid port range: must be between 1 and 65535, and startPort <= endPort");
        }
        boost::asio::io_context io_context;
    
        for (int port = startPort; port <= endPort; ++port) {
            // إنشاء سوكيت TCP جديد لكل منفذ
            boost::asio::ip::tcp::socket socket(io_context);
            boost::system::error_code ec;
            
            // محاولة ربط السوكيت بالمنفذ المحدد
            socket.bind(boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port), ec);
            
            // إذا نجح الربط (المنفذ متاح)
            if (!ec) {
                return port;
            }
            
            // إذا كان الخطأ هو "العنوان مستخدم بالفعل" (EADDRINUSE)
            if (ec == boost::asio::error::address_in_use) {
                continue; // انتقل إلى المنفذ التالي
            }
            
            // أي خطأ آخر (مثل مشاكل في النظام)
            throw std::system_error(ec, "خطأ في ربط المنفذ " + std::to_string(port));
        }
        
        throw std::runtime_error("لم يتم العثور على منفذ متاح في النطاق " + 
                                std::to_string(startPort) + "-" + std::to_string(endPort));
    }



};

