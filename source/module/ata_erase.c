#include "ata_erase.h"
#include "platform.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#ifdef _WIN32
#include <ntddscsi.h>
#include <winioctl.h>
typedef struct {
  BYTE command;
  BYTE command_specific;
  BYTE features;
  BYTE sectors;
  BYTE sector_number;
  BYTE cylinder_low;
  BYTE cylinder_high;
  BYTE drive_head;
} ATA_COMMAND_REGISTER;
typedef struct {
  BYTE command;
  BYTE features;
  BYTE sector_count;
  BYTE sector_number;
  BYTE cylinder_low;
  BYTE cylinder_high;
  BYTE drive_head;
  BYTE reserved[3];
} ATA_COMMAND_REGISTER_48;
static error_code_t ata_pass_through_windows(device_t *device, BYTE command, BYTE features, BYTE sector_count, BYTE sector_number, BYTE cylinder_low,
                                             BYTE cylinder_high, BYTE drive_head, void *data_buffer, DWORD buffer_size, BOOL is_write) {
  if (!device || !device->is_open)
    return ERR_INVALID_ARG;
  DWORD total_buffer_size = sizeof(ATA_PASS_THROUGH_EX) + buffer_size;
  BYTE *full_buffer = (BYTE *)malloc(total_buffer_size);
  if (!full_buffer)
    return ERR_MEMORY;
  memset(full_buffer, 0, total_buffer_size);
  ATA_PASS_THROUGH_EX *pte = (ATA_PASS_THROUGH_EX *)full_buffer;
  pte->Length = sizeof(ATA_PASS_THROUGH_EX);
  pte->TimeOutValue = 30;
  pte->DataBufferOffset = sizeof(ATA_PASS_THROUGH_EX);
  pte->DataTransferLength = buffer_size;
  pte->AtaFlags = is_write ? ATA_FLAGS_DATA_OUT : ATA_FLAGS_DATA_IN;
  if (command == 0xF3 || command == 0xF4 || command == 0xF5 || command == 0xF6)
    pte->AtaFlags = ATA_FLAGS_48BIT_COMMAND;
  pte->CurrentTaskFile[0] = command;
  pte->CurrentTaskFile[1] = features;
  pte->CurrentTaskFile[2] = sector_count;
  pte->CurrentTaskFile[3] = sector_number;
  pte->CurrentTaskFile[4] = cylinder_low;
  pte->CurrentTaskFile[5] = cylinder_high;
  pte->CurrentTaskFile[6] = drive_head;
  if (data_buffer && buffer_size > 0 && is_write) {
    if (pte->DataBufferOffset + buffer_size > total_buffer_size) {
      free(full_buffer);
      return ERR_INVALID_ARG;
    }
    memcpy(full_buffer + pte->DataBufferOffset, data_buffer, buffer_size);
  }
  DWORD bytes_returned;
  BOOL success =
      DeviceIoControl(device->handle, IOCTL_ATA_PASS_THROUGH, full_buffer, total_buffer_size, full_buffer, total_buffer_size, &bytes_returned, NULL);
  if (success) {
    if (pte->CurrentTaskFile[0] & 0x01) {
      free(full_buffer);
      return ERR_READ_DEVICE;
    }
    if (data_buffer && !is_write && buffer_size > 0) {
      if (pte->DataBufferOffset + buffer_size > total_buffer_size) {
        free(full_buffer);
        return ERR_INVALID_ARG;
      }
      memcpy(data_buffer, full_buffer + pte->DataBufferOffset, buffer_size);
    }
    free(full_buffer);
    return ERR_OK;
  }
  free(full_buffer);
  return ERR_READ_DEVICE;
}
static error_code_t ata_identify_windows(device_t *device, uint16_t *identify_data) {
  if (!device || !identify_data)
    return ERR_INVALID_ARG;
  return ata_pass_through_windows(device, 0xEC, 0x00, 0x00, 0x00, 0x00, 0x00, 0xA0, identify_data, 512, FALSE);
}
#else
#include <linux/hdreg.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <limits.h>
#include <scsi/sg.h>
#include <stdbool.h>
static error_code_t ata_pass_through_linux(device_t *device, uint8_t command, uint8_t features, uint8_t sector_count, uint64_t logical_block_address,
                                           void *data_buffer, size_t buffer_size, bool is_write) {
  if (!device || !device->is_open)
    return ERR_INVALID_ARG;
  if (buffer_size > UINT_MAX)
    return ERR_INVALID_ARG;
  struct sg_io_hdr sg_header;
  uint8_t cdb[16];
  uint8_t sense[32];
  uint8_t *data = NULL;
  memset(&sg_header, 0, sizeof(sg_header));
  memset(cdb, 0, sizeof(cdb));
  memset(sense, 0, sizeof(sense));
  if (logical_block_address > 0x0FFFFFFF) {
    cdb[0] = is_write ? 0x8F : 0x8E;
    cdb[1] = 0x00;
    cdb[2] = (logical_block_address >> 40) & 0xFF;
    cdb[3] = (logical_block_address >> 32) & 0xFF;
    cdb[4] = (logical_block_address >> 24) & 0xFF;
    cdb[5] = (logical_block_address >> 16) & 0xFF;
    cdb[6] = (logical_block_address >> 8) & 0xFF;
    cdb[7] = logical_block_address & 0xFF;
    cdb[8] = sector_count;
    cdb[9] = features;
    cdb[10] = command;
    cdb[11] = 0x00;
    cdb[12] = 0x00;
    cdb[13] = 0x00;
    cdb[14] = 0x00;
    cdb[15] = 0x00;
  } else {
    cdb[0] = is_write ? 0x8B : 0x8A;
    cdb[1] = 0x00;
    cdb[2] = (logical_block_address >> 24) & 0xFF;
    cdb[3] = (logical_block_address >> 16) & 0xFF;
    cdb[4] = (logical_block_address >> 8) & 0xFF;
    cdb[5] = logical_block_address & 0xFF;
    cdb[6] = sector_count;
    cdb[7] = features;
    cdb[8] = command;
    cdb[9] = 0x00;
    cdb[10] = 0x00;
    cdb[11] = 0x00;
    cdb[12] = 0x00;
    cdb[13] = 0x00;
    cdb[14] = 0x00;
    cdb[15] = 0x00;
  }
  if (data_buffer && buffer_size > 0) {
    data = (uint8_t *)malloc(buffer_size);
    if (!data)
      return ERR_MEMORY;
    if (is_write)
      memcpy(data, data_buffer, buffer_size);
  }
  sg_header.interface_id = 'S';
  sg_header.dxfer_direction = is_write ? SG_DXFER_TO_DEV : SG_DXFER_FROM_DEV;
  sg_header.cmd_len = sizeof(cdb);
  sg_header.cmdp = cdb;
  sg_header.dxferp = data ? data : data_buffer;
  sg_header.dxfer_len = (unsigned int)buffer_size;
  sg_header.sbp = sense;
  sg_header.mx_sb_len = sizeof(sense);
  sg_header.timeout = 30000;
  int ioctl_result = ioctl(device->handle, SG_IO, &sg_header);
  error_code_t result = ERR_OK;
  if (ioctl_result < 0 || (sg_header.info & SG_INFO_CHECK))
    result = ERR_READ_DEVICE;
  else if (data && !is_write && buffer_size > 0)
    memcpy(data_buffer, data, buffer_size);
  if (data)
    free(data);
  return result;
}
static error_code_t ata_identify_linux(device_t *device, uint16_t *identify_data) {
  if (!device || !identify_data)
    return ERR_INVALID_ARG;
  return ata_pass_through_linux(device, 0xEC, 0x00, 0x01, 0, identify_data, 512, false);
}
#endif
static void parse_identify_data(const uint16_t *data, ata_security_info_t *info) {
  if (!data || !info)
    return;
  memset(info, 0, sizeof(ata_security_info_t));
  for (int word_index = 0; word_index < 20; word_index++) {
    uint16_t word = data[10 + word_index];
    info->model[word_index * 2] = (word >> 8) & 0xFF;
    info->model[word_index * 2 + 1] = word & 0xFF;
  }
  for (int char_index = 19; char_index >= 0 && info->model[char_index] == ' '; char_index--)
    info->model[char_index] = '\0';
  for (int word_index = 0; word_index < 4; word_index++) {
    uint16_t word = data[23 + word_index];
    info->firmware[word_index * 2] = (word >> 8) & 0xFF;
    info->firmware[word_index * 2 + 1] = word & 0xFF;
  }
  for (int char_index = 7; char_index >= 0 && info->firmware[char_index] == ' '; char_index--)
    info->firmware[char_index] = '\0';
  for (int word_index = 0; word_index < 10; word_index++) {
    uint16_t word = data[10 + word_index];
    info->serial[word_index * 2] = (word >> 8) & 0xFF;
    info->serial[word_index * 2 + 1] = word & 0xFF;
  }
  for (int char_index = 19; char_index >= 0 && info->serial[char_index] == ' '; char_index--)
    info->serial[char_index] = '\0';
  if (data[82] & 0x0002)
    info->supported = true;
  if (data[82] & 0x0004)
    info->enhanced_supported = true;
  info->max_sectors_per_command = data[103];
  if (info->max_sectors_per_command == 0)
    info->max_sectors_per_command = 256;
  info->max_lba28 = data[60] | (data[61] << 16);
  info->max_lba48 = ((uint64_t)data[100] | ((uint64_t)data[101] << 16) | ((uint64_t)data[102] << 32) | ((uint64_t)data[103] << 48));
}
error_code_t ata_get_security_info(device_t *device, ata_security_info_t *info) {
  if (!device || !device->is_open || !info)
    return ERR_INVALID_ARG;
  uint16_t identify_data[256];
  memset(identify_data, 0, sizeof(identify_data));
  error_code_t result;
#ifdef _WIN32
  result = ata_identify_windows(device, identify_data);
#else
  result = ata_identify_linux(device, identify_data);
#endif
  if (result != ERR_OK)
    return result;
  parse_identify_data(identify_data, info);
  return ERR_OK;
}
static error_code_t ata_set_password(device_t *device, const char *password, size_t password_length) {
  if (!device || !password || password_length == 0 || password_length > 32)
    return ERR_INVALID_ARG;
  uint8_t security_buffer[512];
  memset(security_buffer, 0, sizeof(security_buffer));
  security_buffer[0] = 0x01;
  security_buffer[2] = (uint8_t)password_length;
  memcpy(security_buffer + 8, password, password_length);
  for (size_t fill_index = password_length; fill_index < 32; fill_index++)
    security_buffer[8 + fill_index] = 0;
#ifdef _WIN32
  return ata_pass_through_windows(device, 0xF1, 0x01, 0x00, 0x00, 0x00, 0x00, 0xA0, security_buffer, 512, true);
#else
  return ata_pass_through_linux(device, 0xF1, 0x01, 0x01, 0, security_buffer, 512, true);
#endif
}
static error_code_t ata_disable_password(device_t *device, const char *password, size_t password_length) {
  if (!device || !password || password_length == 0 || password_length > 32)
    return ERR_INVALID_ARG;
  uint8_t security_buffer[512];
  memset(security_buffer, 0, sizeof(security_buffer));
  security_buffer[0] = 0x00;
  security_buffer[2] = (uint8_t)password_length;
  memcpy(security_buffer + 8, password, password_length);
  for (size_t fill_index = password_length; fill_index < 32; fill_index++)
    security_buffer[8 + fill_index] = 0;
#ifdef _WIN32
  return ata_pass_through_windows(device, 0xF1, 0x00, 0x00, 0x00, 0x00, 0x00, 0xA0, security_buffer, 512, true);
#else
  return ata_pass_through_linux(device, 0xF1, 0x00, 0x01, 0, security_buffer, 512, true);
#endif
}
static error_code_t ata_execute_secure_erase(device_t *device, ata_erase_type_t erase_type, const char *password, size_t password_length) {
  (void)password;
  (void)password_length;
  if (!device || !password || password_length == 0 || password_length > 32)
    return ERR_INVALID_ARG;
  uint8_t command = (erase_type == ATA_ERASE_ENHANCED) ? 0xF5 : 0xF4;
  uint8_t features = 0x00;
#ifdef _WIN32
  return ata_pass_through_windows(device, command, features, 0x00, 0x00, 0x00, 0x00, 0xA0, NULL, 0, false);
#else
  return ata_pass_through_linux(device, command, features, 0x01, 0, NULL, 0, false);
#endif
}
bool ata_is_frozen(device_t *device) {
  if (!device || !device->is_open)
    return true;
#ifdef _WIN32
  uint16_t identify_data[256];
  memset(identify_data, 0, sizeof(identify_data));
  if (ata_identify_windows(device, identify_data) != ERR_OK)
    return true;
  return (identify_data[128] & 0x0008) ? true : false;
#else
  uint16_t identify_data[256];
  memset(identify_data, 0, sizeof(identify_data));
  if (ata_identify_linux(device, identify_data) != ERR_OK)
    return true;
  return (identify_data[128] & 0x0008) != 0;
#endif
}
error_code_t ata_thaw_device(device_t *device) {
  if (!device || !device->is_open)
    return ERR_INVALID_ARG;
  LOG_INFO("Attempting to thaw device from frozen state...");
  if (!ata_is_frozen(device)) {
    LOG_INFO("Device is not frozen, no thaw needed");
    return ERR_OK;
  }
  error_code_t result = ata_set_password(device, "SECUREWIPE", 10);
  if (result != ERR_OK) {
    LOG_WARN("Failed to set temporary password for thaw");
    return result;
  }
  result = ata_disable_password(device, "SECUREWIPE", 10);
  if (result != ERR_OK) {
    LOG_WARN("Failed to disable temporary password after thaw");
    return result;
  }
  LOG_INFO("Device thawed successfully");
  return ERR_OK;
}
error_code_t ata_secure_erase(device_t *device, ata_erase_type_t erase_type, progress_callback_t progress) {
  if (!device || !device->is_open)
    return ERR_INVALID_ARG;
  LOG_INFO("Starting ATA Secure Erase (%s)", erase_type == ATA_ERASE_ENHANCED ? "ENHANCED" : "NORMAL");
  ata_security_info_t security_info;
  error_code_t result = ata_get_security_info(device, &security_info);
  if (result != ERR_OK) {
    LOG_ERROR("Failed to get device security info");
    return result;
  }
  if (!security_info.supported) {
    LOG_ERROR("ATA Security not supported by this device");
    return ERR_PERMISSION;
  }
  if (erase_type == ATA_ERASE_ENHANCED && !security_info.enhanced_supported) {
    LOG_WARN("Enhanced erase not supported, falling back to normal erase");
    erase_type = ATA_ERASE_NORMAL;
  }
  LOG_INFO("Device: %s (FW: %s, SN: %s)", security_info.model, security_info.firmware, security_info.serial);
  LOG_INFO("Security %s, Enhanced %s", security_info.supported ? "supported" : "not supported",
           security_info.enhanced_supported ? "supported" : "not supported");
  if (ata_is_frozen(device)) {
    LOG_WARN("Device is frozen. Attempting to thaw...");
    result = ata_thaw_device(device);
    if (result != ERR_OK) {
      LOG_ERROR("Cannot proceed: device is frozen and thaw failed.");
      LOG_ERROR("Try suspending and resuming system, or use --emergency mode.");
      return ERR_PERMISSION;
    }
  }
  const char *temporary_password = "SECUREWIPE_TEMP";
  size_t password_length = 15;
  if (progress)
    progress(0, 100, 0, "Setting security password");
  result = ata_set_password(device, temporary_password, password_length);
  if (result != ERR_OK) {
    LOG_ERROR("Failed to set security password");
    return result;
  }
  LOG_INFO("Security password set, starting erase operation...");
  if (progress)
    progress(10, 100, 0, "Erasing (this may take several minutes)");
  result = ata_execute_secure_erase(device, erase_type, temporary_password, password_length);
  if (progress)
    progress(90, 100, 0, "Finalizing");
  if (result != ERR_OK) {
    LOG_ERROR("Secure erase command failed");
    ata_disable_password(device, temporary_password, password_length);
    return result;
  }
  LOG_INFO("Secure erase completed, disabling security...");
  result = ata_disable_password(device, temporary_password, password_length);
  if (result != ERR_OK)
    LOG_WARN("Failed to disable security after erase (may need power cycle)");
  if (progress)
    progress(100, 100, 0, "Complete");
  LOG_INFO("ATA Secure Erase completed successfully");
  return ERR_OK;
}
