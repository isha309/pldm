#include "fru_oem_ibm.hpp"

namespace pldm
{
namespace responder
{
namespace oem_ibm_fru
{

void pldm::responder::oem_ibm_fru::Handler::setFruHandler(
    pldm::responder::fru::Handler* handler)
{
    fruHandler = handler;
}

int pldm::responder::oem_ibm_fru::Handler::processOEMfruRecord(
    const std::vector<uint8_t>& fruData)
{
    auto record =
        reinterpret_cast<const pldm_fru_record_data_format*>(fruData.data());
    if (record == NULL)
    {
        return PLDM_ERROR;
    }

    auto& entityAssociationMap = getAssociateEntityMap();
    uint16_t fruRSI = le16toh(record->record_set_id);

    const uint8_t* data = fruData.data();
    data += sizeof(pldm_fru_record_data_format) - sizeof(pldm_fru_record_tlv);
    for (int i = 0; i < record->num_fru_fields; i++)
    {
        auto tlv = reinterpret_cast<const pldm_fru_record_tlv*>(data);
        if (tlv == NULL)
        {
            return PLDM_ERROR;
        }

        if ((tlv->type == PLDM_OEM_FRU_FIELD_TYPE_PCIE_CONFIG_SPACE_DATA))
        {
            auto pcieData =
                reinterpret_cast<const PcieConfigSpaceData*>(tlv->value);

            if (pcieData == NULL)
            {
                return PLDM_ERROR;
            }

            std::stringstream vId;
            vId << "0x" << std::setfill('0') << std::setw(4) << std::hex
                << htole16(pcieData->vendorId);
            auto vendorId = vId.str();

            std::stringstream dId;
            dId << "0x" << std::setfill('0') << std::setw(4) << std::hex
                << htole16(pcieData->deviceId);
            auto deviceId = dId.str();

            std::stringstream rId;
            rId << "0x" << std::setfill('0') << std::setw(2) << std::hex
                << htole16(pcieData->revisionId);
            auto revisionId = rId.str();

            std::stringstream ss;
            ss << "0x";
            for (int ele : pcieData->classCode)
            {
                ss << std::setfill('0') << std::setw(2) << std::hex << ele;
            }
            auto classCode = ss.str();

            std::stringstream subSysVenId;
            subSysVenId << "0x" << std::setfill('0') << std::setw(4) << std::hex
                        << htole16(pcieData->subSystemVendorId);
            auto subSystemVendorId = subSysVenId.str();

            std::stringstream subSysId;
            subSysId << "0x" << std::setfill('0') << std::setw(4) << std::hex
                     << htole16(pcieData->subSystemId);
            auto subSystemId = subSysId.str();

            updateDBusProperty(fruRSI, entityAssociationMap, vendorId, deviceId,
                               revisionId, classCode, subSystemVendorId,
                               subSystemId);
        }
        if (tlv->type == PLDM_OEM_FRU_FIELD_TYPE_FIRMWARE_UAK)
        {
            std::vector<uint8_t> value(&tlv->value[0],
                                       &tlv->value[tlv->length]);
            setFirmwareUAK(value);
        }
        data += sizeof(pldm_fru_record_tlv) - 1 + tlv->length;
    }

    return PLDM_SUCCESS;
}

void Handler::updateDBusProperty(
    uint16_t fruRSI, const AssociatedEntityMap& fruAssociationMap,
    const std::string& vendorId, const std::string& deviceId,
    const std::string& revisionId, const std::string& classCode,
    const std::string& subSystemVendorId, const std::string& subSystemId)
{
    uint16_t entityType{};
    uint16_t entityInstanceNum{};
    uint16_t containerId{};
    uint16_t terminusHandle{};
    const pldm_pdr_record* record{};

    record = pldm_pdr_fru_record_set_find_by_rsi(
        pdrRepo, fruRSI, &terminusHandle, &entityType, &entityInstanceNum,
        &containerId, false);

    if (record)
    {
        for (const auto& [key, value] : fruAssociationMap)
        {
            if (entityInstanceNum == value.entity_instance_num &&
                entityType == value.entity_type &&
                containerId == value.entity_container_id)
            {
                if (!(pldm::responder::utils::checkIfIBMCableCard(key)))
                {
                    setFruPresence(key);
                }

                dbus_map_update(key, "Function0VendorId", vendorId);
                dbus_map_update(key, "Function0DeviceId", deviceId);
                dbus_map_update(key, "Function0RevisionId", revisionId);
                dbus_map_update(key, "Function0ClassCode", classCode);
                dbus_map_update(key, "Function0SubsystemVendorId",
                                subSystemVendorId);
                dbus_map_update(key, "Function0SubsystemId", subSystemId);
            }
        }
    }
}
void Handler::dbus_map_update(const std::string& adapterObjPath,
                              const std::string& propertyName,
                              const std::string& propValue)
{
    pldm::utils::PropertyValue value = propValue;
    pldm::utils::DBusMapping dbusMapping;
    dbusMapping.objectPath = adapterObjPath;
    dbusMapping.interface = "xyz.openbmc_project.Inventory.Item.PCIeDevice";
    dbusMapping.propertyName = propertyName;
    dbusMapping.propertyType = "string";
    try
    {
        pldm::utils::DBusHandler().setDbusProperty(dbusMapping, value);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Failed To set " << propertyName << "property"
                  << "ERROR=" << e.what() << std::endl;
    }
}

void Handler::setFruPresence(const std::string& adapterObjPath)
{
    pldm::utils::PropertyValue value{true};
    pldm::utils::DBusMapping dbusMapping;
    dbusMapping.objectPath = adapterObjPath;
    dbusMapping.interface = "xyz.openbmc_project.Inventory.Item";
    dbusMapping.propertyName = "Present";
    dbusMapping.propertyType = "bool";
    try
    {
        pldm::utils::DBusHandler().setDbusProperty(dbusMapping, value);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Failed to set the present property"
                  << "ERROR=" << e.what() << std::endl;
    }
}

void Handler::setFirmwareUAK(std::vector<uint8_t> data)
{
    static constexpr auto uakObjPath = "/com/ibm/VPD/Manager";
    static constexpr auto uakInterface = "com.ibm.VPD.Manager";

    static constexpr auto fruPath =
        "/xyz/openbmc_project/inventory/system/chassis/motherboard";
    static constexpr auto fruRecord = "UTIL";
    static constexpr auto fruKeyword = "D8";

    auto& bus = pldm::utils::DBusHandler::getBus();
    try
    {
        auto service =
            pldm::utils::DBusHandler().getService(uakObjPath, uakInterface);
        auto method = bus.new_method_call(service.c_str(), uakObjPath,
                                          uakInterface, "WriteKeyword");
        method.append(static_cast<sdbusplus::message::object_path>(fruPath),
                      fruRecord, fruKeyword, data);
        bus.call_noreply(
            method,
            std::chrono::duration_cast<microsec>(sec(DBUS_TIMEOUT)).count());
    }
    catch (const std::exception& e)
    {
        std::cerr << "failed to make a DBus call to VPD manager, ERROR="
                  << e.what() << "\n";
        return;
    }
}
} // namespace oem_ibm_fru
} // namespace responder
} // namespace pldm
