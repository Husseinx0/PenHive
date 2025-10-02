#pragma once 
#include "API/common.hpp"
using namespace drogon;
class VirtualMachineuploadDiskController;
class VirtualMachineController : public drogon::HttpController<VirtualMachineController> {
public:
    METHOD_LIST_BEGIN
    // use METHOD_ADD to add your custom processing function here;
    ADD_METHOD_TO(VirtualMachineController::uploadFile, "/api/upload", {Post}); // path is /VirtualMachine/upload
    // METHOD_ADD(VirtualMachineController::get, "/get/{1}", Get); // path is /VirtualMachine/get/{arg1}
    // METHOD_ADD(VirtualMachineController::your_method_name, "/{2}/{1}", Get); // path is /VirtualMachine/{arg2}/{arg1}
    // ADD_METHOD_TO(VirtualMachineController::your_method_name, "/{1}/{2}", Get); // path is /VirtualMachine/{arg1}/{arg2}
    METHOD_LIST_END

    VirtualMachineController() = default;
    virtual ~VirtualMachineController() = default;

    // your declaration of processing function maybe like this:
    // void your_method_name(const HttpRequestPtr& req,
    //                      std::function<void(const HttpResponsePtr&)>&& callback,
    //                      std::string arg1,
    //                      int arg2);
private:
  drogon::Task<> 
    uploadFile(const drogon::HttpRequestPtr& req,std::function<void(const drogon::HttpResponsePtr&)>&& callback){
        std::async_launch(std::launch::async, [](){
            // Long-running operation here
        });
        auto files = req->getFiles();
        if (files.empty()) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k400BadRequest);
            resp->setBody("No file uploaded");      
            callback(resp);
            co_return;
        }
        const auto& file = files[0]; // Assuming single file upload
        std::string uploadPath = "/var/lib/penhive/uploads/" + file.getFileName();
        if (!file.moveTo(uploadPath)) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k500InternalServerError);
            resp->setBody("Failed to save uploaded file");
            callback(resp); 
        }
    }
};
class VMwareIntegration; // Forward declaration
class VirtualMachineuploadDiskController {
public:
    

};


/*
// AsyncTaskManager.h
#pragma once
#include <drogon/drogon.h>
#include <unordered_map>
#include <atomic>

enum class TaskStatus {
    PENDING,
    RUNNING,
    COMPLETED,
    FAILED
};

struct AsyncTask {
    std::string task_id;
    TaskStatus status;
    std::string operation;
    std::string vm_id;
    Json::Value result;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point updated_at;
};

class AsyncTaskManager {
public:
    static AsyncTaskManager& instance();
    
    std::string createTask(const std::string& operation, const std::string& vm_id);
    TaskStatus getTaskStatus(const std::string& task_id);
    Json::Value getTaskResult(const std::string& task_id);
    void updateTaskStatus(const std::string& task_id, TaskStatus status, const Json::Value& result = Json::Value());
    
private:
    std::unordered_map<std::string, AsyncTask> tasks_;
    std::atomic<int> task_counter_{0};
    std::mutex mutex_;
};
// VMAsyncController.h
#pragma once
#include <drogon/HttpController.h>
#include "VMwareIntegration.h"
#include "AsyncTaskManager.h"

namespace api::v1 {
    class VMAsyncController : public HttpController<VMAsyncController> {
    public:
        METHOD_LIST_BEGIN
        ADD_METHOD_TO(VMAsyncController::startVM, "/api/v1/vm/{1}/start", Post);
        ADD_METHOD_TO(VMAsyncController::stopVM, "/api/v1/vm/{1}/stop", Post);
        ADD_METHOD_TO(VMAsyncController::getTaskStatus, "/api/v1/tasks/{1}", Get);
        METHOD_LIST_END
        
        Task<> startVM(const HttpRequestPtr& req,
                      std::function<void(const HttpResponsePtr&)>&& callback,
                      std::string vmId);
                      
        Task<> getTaskStatus(const HttpRequestPtr& req,
                           std::function<void(const HttpResponsePtr&)>&& callback,
                           std::string taskId);
                           
    private:
        VMwareIntegration vmware_{"https://vcenter.example.com", "username", "password"};
    };
}
// VMAsyncController.cc
#include "VMAsyncController.h"

using namespace api::v1;

Task<> VMAsyncController::startVM(const HttpRequestPtr& req,
                                 std::function<void(const HttpResponsePtr&)>&& callback,
                                 std::string vmId) {
    
    // إنشاء مهمة غير متزامنة فوراً
    auto task_id = AsyncTaskManager::instance().createTask("start_vm", vmId);
    
    // الرد الفوري بمعلومات المهمة
    Json::Value response;
    response["status"] = "accepted";
    response["task_id"] = task_id;
    response["message"] = "VM start operation queued";
    
    auto immediate_resp = HttpResponse::newHttpJsonResponse(response);
    immediate_resp->setStatusCode(k202Accepted); // 202 Accepted
    callback(immediate_resp);
    
    // تنفيذ العملية فعلياً في الخلفية
    co_await std::async(std::launch::async, [this, task_id, vmId]() {
        try {
            bool success = vmware_.powerOnVM(vmId).get(); // انتظار النتيجة
            
            Json::Value result;
            if (success) {
                result["status"] = "success";
                result["message"] = "VM started successfully";
                AsyncTaskManager::instance().updateTaskStatus(task_id, TaskStatus::COMPLETED, result);
            } else {
                result["status"] = "error";
                result["message"] = "Failed to start VM";
                AsyncTaskManager::instance().updateTaskStatus(task_id, TaskStatus::FAILED, result);
            }
        } catch (const std::exception& e) {
            Json::Value error_result;
            error_result["status"] = "error";
            error_result["message"] = std::string("Exception: ") + e.what();
            AsyncTaskManager::instance().updateTaskStatus(task_id, TaskStatus::FAILED, error_result);
        }
    });
}

Task<> VMAsyncController::getTaskStatus(const HttpRequestPtr& req,
                                      std::function<void(const HttpResponsePtr&)>&& callback,
                                      std::string taskId) {
    
    auto status = AsyncTaskManager::instance().getTaskStatus(taskId);
    auto result = AsyncTaskManager::instance().getTaskResult(taskId);
    
    Json::Value response;
    response["task_id"] = taskId;
    
    switch (status) {
        case TaskStatus::PENDING:
            response["status"] = "pending";
            response["message"] = "Task is waiting to be processed";
            break;
        case TaskStatus::RUNNING:
            response["status"] = "running";
            response["message"] = "Task is currently being processed";
            break;
        case TaskStatus::COMPLETED:
            response["status"] = "completed";
            response["result"] = result;
            break;
        case TaskStatus::FAILED:
            response["status"] = "failed";
            response["error"] = result;
            break;
    }
    
    auto resp = HttpResponse::newHttpJsonResponse(response);
    callback(resp);
    co_return;
}
*/