// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

#include <memory>

#include "ash/components/disks/disk.h"
#include "ash/components/disks/mock_disk_mount_manager.h"
#include "ash/constants/ash_features.h"
#include "base/base64.h"
#include "base/bind.h"
#include "base/cxx17_backports.h"
#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/test/mock_callback.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/ash/crostini/crostini_manager.h"
#include "chrome/browser/ash/crostini/crostini_pref_names.h"
#include "chrome/browser/ash/crostini/fake_crostini_features.h"
#include "chrome/browser/ash/drive/drivefs_test_support.h"
#include "chrome/browser/ash/file_manager/file_watcher.h"
#include "chrome/browser/ash/file_manager/mount_test_util.h"
#include "chrome/browser/ash/file_manager/path_util.h"
#include "chrome/browser/ash/file_manager/volume_manager.h"
#include "chrome/browser/ash/file_system_provider/icon_set.h"
#include "chrome/browser/ash/file_system_provider/provided_file_system_info.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chromeos/extensions/file_manager/event_router.h"
#include "chrome/browser/chromeos/extensions/file_manager/event_router_factory.h"
#include "chrome/browser/chromeos/extensions/file_manager/private_api_misc.h"
#include "chrome/browser/chromeos/policy/dlp/dlp_rules_manager.h"
#include "chrome/browser/chromeos/policy/dlp/dlp_rules_manager_factory.h"
#include "chrome/browser/chromeos/policy/dlp/mock_dlp_rules_manager.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/extensions/extension_function_test_utils.h"
#include "chrome/browser/signin/identity_manager_factory.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/extensions/api/file_system_provider_capabilities/file_system_provider_capabilities_handler.h"
#include "chromeos/dbus/concierge/concierge_service.pb.h"
#include "chromeos/dbus/cros_disks/cros_disks_client.h"
#include "chromeos/dbus/dlp/dlp_client.h"
#include "chromeos/dbus/dlp/dlp_service.pb.h"
#include "components/drive/drive_pref_names.h"
#include "components/prefs/pref_service.h"
#include "components/signin/public/identity_manager/identity_test_utils.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/test_navigation_observer.h"
#include "extensions/common/extension.h"
#include "extensions/common/install_warning.h"
#include "google_apis/common/test_util.h"
#include "storage/browser/file_system/external_mount_points.h"

using ::testing::_;
using ::testing::ReturnRef;

namespace {

using ::ash::disks::Disk;
using ::ash::disks::DiskMountManager;
using ::ash::disks::FormatFileSystemType;

struct TestDiskInfo {
  const char* file_path;
  bool write_disabled_by_policy;
  const char* device_label;
  const char* drive_label;
  const char* vendor_id;
  const char* vendor_name;
  const char* product_id;
  const char* product_name;
  const char* fs_uuid;
  const char* storage_device_path;
  chromeos::DeviceType device_type;
  uint64_t size_in_bytes;
  bool is_parent;
  bool is_read_only_hardware;
  bool has_media;
  bool on_boot_device;
  bool on_removable_device;
  bool is_hidden;
  const char* file_system_type;
  const char* base_mount_path;
};

struct TestMountPoint {
  std::string source_path;
  std::string mount_path;
  chromeos::MountType mount_type;
  ash::disks::MountCondition mount_condition;

  // -1 if there is no disk info.
  int disk_info_index;
};

TestDiskInfo kTestDisks[] = {{"file_path1",
                              false,
                              "device_label1",
                              "drive_label1",
                              "0123",
                              "vendor1",
                              "abcd",
                              "product1",
                              "FFFF-FFFF",
                              "storage_device_path1",
                              chromeos::DEVICE_TYPE_USB,
                              1073741824,
                              false,
                              false,
                              false,
                              false,
                              false,
                              false,
                              "exfat",
                              ""},
                             {"file_path2",
                              false,
                              "device_label2",
                              "drive_label2",
                              "4567",
                              "vendor2",
                              "cdef",
                              "product2",
                              "0FFF-FFFF",
                              "storage_device_path2",
                              chromeos::DEVICE_TYPE_MOBILE,
                              47723,
                              true,
                              true,
                              true,
                              true,
                              false,
                              false,
                              "exfat",
                              ""},
                             {"file_path3",
                              true,  // write_disabled_by_policy
                              "device_label3",
                              "drive_label3",
                              "89ab",
                              "vendor3",
                              "ef01",
                              "product3",
                              "00FF-FFFF",
                              "storage_device_path3",
                              chromeos::DEVICE_TYPE_OPTICAL_DISC,
                              0,
                              true,
                              false,  // is_hardware_read_only
                              false,
                              true,
                              false,
                              false,
                              "exfat",
                              ""}};

void AddLocalFileSystem(Profile* profile, base::FilePath root) {
  const char kLocalMountPointName[] = "local";
  const char kTestFileContent[] = "The five boxing wizards jumped quickly";

  {
    base::ScopedAllowBlockingForTesting allow_io;
    ASSERT_TRUE(base::CreateDirectory(root.AppendASCII("test_dir")));
    ASSERT_TRUE(google_apis::test_util::WriteStringToFile(
        root.AppendASCII("test_dir").AppendASCII("test_file.txt"),
        kTestFileContent));
  }

  ASSERT_TRUE(profile->GetMountPoints()->RegisterFileSystem(
      kLocalMountPointName, storage::kFileSystemTypeLocal,
      storage::FileSystemMountOption(), root));
  file_manager::VolumeManager::Get(profile)->AddVolumeForTesting(
      root, file_manager::VOLUME_TYPE_TESTING, chromeos::DEVICE_TYPE_UNKNOWN,
      false /* read_only */);
}

}  // namespace

class FileManagerPrivateApiTest : public extensions::ExtensionApiTest {
 public:
  FileManagerPrivateApiTest() : disk_mount_manager_mock_(nullptr) {
    InitMountPoints();
  }

  ~FileManagerPrivateApiTest() override {
    DCHECK(!disk_mount_manager_mock_);
    DCHECK(!event_router_);
  }

  bool SetUpUserDataDirectory() override {
    return drive::SetUpUserDataDirectoryForDriveFsTest();
  }

  void SetUpOnMainThread() override {
    extensions::ExtensionApiTest::SetUpOnMainThread();
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());

    event_router_ = file_manager::EventRouterFactory::GetForProfile(profile());
  }

  void TearDownOnMainThread() override {
    event_router_ = nullptr;
    extensions::ExtensionApiTest::TearDownOnMainThread();
  }

  // ExtensionApiTest override
  void SetUpInProcessBrowserTestFixture() override {
    extensions::ExtensionApiTest::SetUpInProcessBrowserTestFixture();
    disk_mount_manager_mock_ = new ash::disks::MockDiskMountManager;
    DiskMountManager::InitializeForTesting(disk_mount_manager_mock_);
    disk_mount_manager_mock_->SetupDefaultReplies();

    // override mock functions.
    ON_CALL(*disk_mount_manager_mock_, FindDiskBySourcePath(_)).WillByDefault(
        Invoke(this, &FileManagerPrivateApiTest::FindVolumeBySourcePath));
    EXPECT_CALL(*disk_mount_manager_mock_, disks())
        .WillRepeatedly(ReturnRef(volumes_));
    EXPECT_CALL(*disk_mount_manager_mock_, mount_points())
        .WillRepeatedly(ReturnRef(mount_points_));
  }

  // ExtensionApiTest override
  void TearDownInProcessBrowserTestFixture() override {
    DiskMountManager::Shutdown();
    disk_mount_manager_mock_ = nullptr;

    extensions::ExtensionApiTest::TearDownInProcessBrowserTestFixture();
  }

 private:
  void InitMountPoints() {
    const TestMountPoint kTestMountPoints[] = {
      {
        "device_path1",
        chromeos::CrosDisksClient::GetRemovableDiskMountPoint().AppendASCII(
            "mount_path1").AsUTF8Unsafe(),
        chromeos::MOUNT_TYPE_DEVICE,
        ash::disks::MOUNT_CONDITION_NONE,
        0
      },
      {
        "device_path2",
        chromeos::CrosDisksClient::GetRemovableDiskMountPoint().AppendASCII(
            "mount_path2").AsUTF8Unsafe(),
        chromeos::MOUNT_TYPE_DEVICE,
        ash::disks::MOUNT_CONDITION_NONE,
        1
      },
      {
        "device_path3",
        chromeos::CrosDisksClient::GetRemovableDiskMountPoint().AppendASCII(
            "mount_path3").AsUTF8Unsafe(),
        chromeos::MOUNT_TYPE_DEVICE,
        ash::disks::MOUNT_CONDITION_NONE,
        2
      },
      {
        // Set source path inside another mounted volume.
        chromeos::CrosDisksClient::GetRemovableDiskMountPoint().AppendASCII(
            "mount_path3/archive.zip").AsUTF8Unsafe(),
        chromeos::CrosDisksClient::GetArchiveMountPoint().AppendASCII(
            "archive_mount_path").AsUTF8Unsafe(),
        chromeos::MOUNT_TYPE_ARCHIVE,
        ash::disks::MOUNT_CONDITION_NONE,
        -1
      }
    };

    for (size_t i = 0; i < base::size(kTestMountPoints); i++) {
      mount_points_.insert(DiskMountManager::MountPointMap::value_type(
          kTestMountPoints[i].mount_path,
          DiskMountManager::MountPointInfo(kTestMountPoints[i].source_path,
                                           kTestMountPoints[i].mount_path,
                                           kTestMountPoints[i].mount_type,
                                           kTestMountPoints[i].mount_condition)
      ));
      int disk_info_index = kTestMountPoints[i].disk_info_index;
      if (kTestMountPoints[i].disk_info_index >= 0) {
        EXPECT_GT(base::size(kTestDisks), static_cast<size_t>(disk_info_index));
        if (static_cast<size_t>(disk_info_index) >= base::size(kTestDisks))
          return;

        std::unique_ptr<Disk> disk =
            Disk::Builder()
                .SetDevicePath(kTestMountPoints[i].source_path)
                .SetMountPath(kTestMountPoints[i].mount_path)
                .SetWriteDisabledByPolicy(
                    kTestDisks[disk_info_index].write_disabled_by_policy)
                .SetFilePath(kTestDisks[disk_info_index].file_path)
                .SetDeviceLabel(kTestDisks[disk_info_index].device_label)
                .SetDriveLabel(kTestDisks[disk_info_index].drive_label)
                .SetVendorId(kTestDisks[disk_info_index].vendor_id)
                .SetVendorName(kTestDisks[disk_info_index].vendor_name)
                .SetProductId(kTestDisks[disk_info_index].product_id)
                .SetProductName(kTestDisks[disk_info_index].product_name)
                .SetFileSystemUUID(kTestDisks[disk_info_index].fs_uuid)
                .SetStorageDevicePath(
                    kTestDisks[disk_info_index].storage_device_path)
                .SetDeviceType(kTestDisks[disk_info_index].device_type)
                .SetSizeInBytes(kTestDisks[disk_info_index].size_in_bytes)
                .SetIsParent(kTestDisks[disk_info_index].is_parent)
                .SetIsReadOnlyHardware(
                    kTestDisks[disk_info_index].is_read_only_hardware)
                .SetHasMedia(kTestDisks[disk_info_index].has_media)
                .SetOnBootDevice(kTestDisks[disk_info_index].on_boot_device)
                .SetOnRemovableDevice(
                    kTestDisks[disk_info_index].on_removable_device)
                .SetIsHidden(kTestDisks[disk_info_index].is_hidden)
                .SetFileSystemType(kTestDisks[disk_info_index].file_system_type)
                .SetBaseMountPath(kTestDisks[disk_info_index].base_mount_path)
                .Build();

        volumes_.insert(DiskMountManager::DiskMap::value_type(
            kTestMountPoints[i].source_path, std::move(disk)));
      }
    }
  }

  const Disk* FindVolumeBySourcePath(const std::string& source_path) {
    auto volume_it = volumes_.find(source_path);
    return (volume_it == volumes_.end()) ? nullptr : volume_it->second.get();
  }

 protected:
  void SshfsMount(const std::string& source_path,
                  const std::string& source_format,
                  const std::string& mount_label,
                  const std::vector<std::string>& mount_options,
                  chromeos::MountType type,
                  chromeos::MountAccessMode access_mode,
                  DiskMountManager::MountPathCallback callback) {
    auto mount_point_info = DiskMountManager::MountPointInfo(
        source_path, "/media/fuse/" + mount_label,
        chromeos::MountType::MOUNT_TYPE_NETWORK_STORAGE,
        ash::disks::MountCondition::MOUNT_CONDITION_NONE);
    disk_mount_manager_mock_->NotifyMountEvent(
        DiskMountManager::MountEvent::MOUNTING,
        chromeos::MountError::MOUNT_ERROR_NONE, mount_point_info);
    std::move(callback).Run(chromeos::MOUNT_ERROR_NONE, mount_point_info);
  }

  void ExpectCrostiniMount() {
    std::string known_hosts;
    base::Base64Encode("[hostname]:2222 pubkey", &known_hosts);
    std::string identity;
    base::Base64Encode("privkey", &identity);
    std::vector<std::string> mount_options = {
        "UserKnownHostsBase64=" + known_hosts, "IdentityBase64=" + identity,
        "Port=2222"};
    EXPECT_CALL(*disk_mount_manager_mock_,
                MountPath("sshfs://testuser@hostname:", "",
                          "crostini_user_termina_penguin", mount_options,
                          chromeos::MOUNT_TYPE_NETWORK_STORAGE,
                          chromeos::MOUNT_ACCESS_MODE_READ_WRITE, _))
        .WillOnce(Invoke(this, &FileManagerPrivateApiTest::SshfsMount));
  }

  base::ScopedTempDir temp_dir_;
  ash::disks::MockDiskMountManager* disk_mount_manager_mock_;
  DiskMountManager::DiskMap volumes_;
  DiskMountManager::MountPointMap mount_points_;
  file_manager::EventRouter* event_router_ = nullptr;
};

IN_PROC_BROWSER_TEST_F(FileManagerPrivateApiTest, Mount) {
  using ash::file_system_provider::IconSet;
  profile()->GetPrefs()->SetBoolean(drive::prefs::kDisableDrive, true);

  // Add a provided file system, to test passing the |configurable| and
  // |source| flags properly down to Files app.
  IconSet icon_set;
  icon_set.SetIcon(IconSet::IconSize::SIZE_16x16,
                   GURL("chrome://resources/testing-provider-id-16.jpg"));
  icon_set.SetIcon(IconSet::IconSize::SIZE_32x32,
                   GURL("chrome://resources/testing-provider-id-32.jpg"));
  ash::file_system_provider::ProvidedFileSystemInfo info(
      "testing-provider-id", ash::file_system_provider::MountOptions(),
      base::FilePath(), true /* configurable */, false /* watchable */,
      extensions::SOURCE_NETWORK, icon_set);

  file_manager::VolumeManager::Get(browser()->profile())
      ->AddVolumeForTesting(file_manager::Volume::CreateForProvidedFileSystem(
          info, file_manager::MOUNT_CONTEXT_AUTO));

  // We will call fileManagerPrivate.unmountVolume once. To test that method, we
  // check that UnmountPath is really called with the same value.
  EXPECT_CALL(*disk_mount_manager_mock_, UnmountPath(_, _)).Times(0);
  EXPECT_CALL(
      *disk_mount_manager_mock_,
      UnmountPath(chromeos::CrosDisksClient::GetRemovableDiskMountPoint()
                      .AppendASCII("mount_path1")
                      .AsUTF8Unsafe(),
                  _))
      .Times(1);
  EXPECT_CALL(*disk_mount_manager_mock_,
              UnmountPath(chromeos::CrosDisksClient::GetArchiveMountPoint()
                              .AppendASCII("archive_mount_path")
                              .AsUTF8Unsafe(),
                          _))
      .Times(1);

  ASSERT_TRUE(RunExtensionTest("file_browser/mount_test", {},
                               {.load_as_component = true}))
      << message_;
}

IN_PROC_BROWSER_TEST_F(FileManagerPrivateApiTest, FormatVolume) {
  // Catch-all rule to ensure that FormatMountedDevice does not get called with
  // other arguments that don't match the rules below.
  EXPECT_CALL(*disk_mount_manager_mock_, FormatMountedDevice(_, _, _)).Times(0);

  EXPECT_CALL(*disk_mount_manager_mock_,
              FormatMountedDevice(
                  chromeos::CrosDisksClient::GetRemovableDiskMountPoint()
                      .AppendASCII("mount_path1")
                      .AsUTF8Unsafe(),
                  FormatFileSystemType::kVfat, "NEWLABEL1"))
      .Times(1);
  EXPECT_CALL(*disk_mount_manager_mock_,
              FormatMountedDevice(
                  chromeos::CrosDisksClient::GetRemovableDiskMountPoint()
                      .AppendASCII("mount_path2")
                      .AsUTF8Unsafe(),
                  FormatFileSystemType::kExfat, "NEWLABEL2"))
      .Times(1);
  EXPECT_CALL(*disk_mount_manager_mock_,
              FormatMountedDevice(
                  chromeos::CrosDisksClient::GetRemovableDiskMountPoint()
                      .AppendASCII("mount_path3")
                      .AsUTF8Unsafe(),
                  FormatFileSystemType::kNtfs, "NEWLABEL3"))
      .Times(1);

  ASSERT_TRUE(RunExtensionTest("file_browser/format_test", {},
                               {.load_as_component = true}));
}

IN_PROC_BROWSER_TEST_F(FileManagerPrivateApiTest, Permissions) {
  EXPECT_TRUE(RunExtensionTest("file_browser/permissions", {},
                               {.ignore_manifest_warnings = true}));
  const extensions::Extension* extension = GetSingleLoadedExtension();
  ASSERT_TRUE(extension);
  ASSERT_EQ(1u, extension->install_warnings().size());
  const extensions::InstallWarning& warning = extension->install_warnings()[0];
  EXPECT_EQ("fileManagerPrivate", warning.key);
}

IN_PROC_BROWSER_TEST_F(FileManagerPrivateApiTest, ContentChecksum) {
  AddLocalFileSystem(browser()->profile(), temp_dir_.GetPath());

  ASSERT_TRUE(RunExtensionTest("file_browser/content_checksum_test", {},
                               {.load_as_component = true}));
}

IN_PROC_BROWSER_TEST_F(FileManagerPrivateApiTest, Recent) {
  const base::FilePath downloads_dir = temp_dir_.GetPath();

  ASSERT_TRUE(file_manager::VolumeManager::Get(browser()->profile())
                  ->RegisterDownloadsDirectoryForTesting(downloads_dir));

  // Create test files.
  {
    base::ScopedAllowBlockingForTesting allow_io;
    base::File image_file(downloads_dir.Append("all-justice.jpg"),
                          base::File::FLAG_CREATE | base::File::FLAG_WRITE);
    ASSERT_TRUE(image_file.IsValid());
    base::File audio_file(downloads_dir.Append("all-justice.mp3"),
                          base::File::FLAG_CREATE | base::File::FLAG_WRITE);
    ASSERT_TRUE(audio_file.IsValid());
    base::File video_file(downloads_dir.Append("all-justice.mp4"),
                          base::File::FLAG_CREATE | base::File::FLAG_WRITE);
    ASSERT_TRUE(video_file.IsValid());
  }

  ASSERT_TRUE(RunExtensionTest("file_browser/recent_test", {},
                               {.load_as_component = true}));
}

IN_PROC_BROWSER_TEST_F(FileManagerPrivateApiTest, MediaMetadata) {
  const base::FilePath test_dir = temp_dir_.GetPath();
  AddLocalFileSystem(browser()->profile(), test_dir);

  // Get source media/test/data directory path.
  base::FilePath root_dir;
  CHECK(base::PathService::Get(base::DIR_SOURCE_ROOT, &root_dir));
  const base::FilePath media_test_data_dir =
      root_dir.AppendASCII("media").AppendASCII("test").AppendASCII("data");

  // Returns a path to a media/test/data test file.
  auto get_media_test_data_file = [&](const std::string& file) {
    return media_test_data_dir.Append(base::FilePath::FromUTF8Unsafe(file));
  };

  // Create media test files.
  {
    base::ScopedAllowBlockingForTesting allow_io;

    const base::FilePath video = get_media_test_data_file("90rotation.mp4");
    ASSERT_TRUE(base::CopyFile(video, test_dir.Append(video.BaseName())));

    const base::FilePath audio = get_media_test_data_file("id3_png_test.mp3");
    ASSERT_TRUE(base::CopyFile(audio, test_dir.Append(audio.BaseName())));
  }

  // Get source chrome/test/data/chromeos/file_manager directory path.
  const base::FilePath files_test_data_dir = root_dir.AppendASCII("chrome")
                                                 .AppendASCII("test")
                                                 .AppendASCII("data")
                                                 .AppendASCII("chromeos")
                                                 .AppendASCII("file_manager");

  // Returns a path to a chrome/test/data/chromeos/file_manager test file.
  auto get_files_test_data_dir = [&](const std::string& file) {
    return files_test_data_dir.Append(base::FilePath::FromUTF8Unsafe(file));
  };

  // Create files test files.
  {
    base::ScopedAllowBlockingForTesting allow_io;

    const base::FilePath broke = get_files_test_data_dir("broken.jpg");
    ASSERT_TRUE(base::CopyFile(broke, test_dir.Append(broke.BaseName())));

    const base::FilePath empty = get_files_test_data_dir("empty.txt");
    ASSERT_TRUE(base::CopyFile(empty, test_dir.Append(empty.BaseName())));

    const base::FilePath image = get_files_test_data_dir("image3.jpg");
    ASSERT_TRUE(base::CopyFile(image, test_dir.Append(image.BaseName())));
  }

  ASSERT_TRUE(RunExtensionTest("file_browser/media_metadata", {},
                               {.load_as_component = true}));
}

IN_PROC_BROWSER_TEST_F(FileManagerPrivateApiTest, Crostini) {
  crostini::FakeCrostiniFeatures crostini_features;
  crostini_features.set_is_allowed_now(true);
  crostini_features.set_enabled(true);

  // Setup CrostiniManager for testing.
  crostini::CrostiniManager* crostini_manager =
      crostini::CrostiniManager::GetForProfile(browser()->profile());
  crostini_manager->set_skip_restart_for_testing();
  crostini_manager->AddRunningVmForTesting(crostini::kCrostiniDefaultVmName);
  crostini_manager->AddRunningContainerForTesting(
      crostini::kCrostiniDefaultVmName,
      crostini::ContainerInfo(crostini::kCrostiniDefaultContainerName,
                              "testuser", "/home/testuser", "PLACEHOLDER_IP"));

  ExpectCrostiniMount();

  // Add 'testing' volume with 'test_dir', create 'share_dir' in Downloads.
  AddLocalFileSystem(browser()->profile(), temp_dir_.GetPath());
  base::FilePath downloads;
  ASSERT_TRUE(
      storage::ExternalMountPoints::GetSystemInstance()->GetRegisteredPath(
          file_manager::util::GetDownloadsMountPointName(browser()->profile()),
          &downloads));
  // Setup prefs guest_os.paths_shared_to_vms.
  base::FilePath shared1 = downloads.AppendASCII("shared1");
  base::FilePath shared2 = downloads.AppendASCII("shared2");
  {
    base::ScopedAllowBlockingForTesting allow_io;
    ASSERT_TRUE(base::CreateDirectory(downloads.AppendASCII("share_dir")));
    ASSERT_TRUE(base::CreateDirectory(shared1));
    ASSERT_TRUE(base::CreateDirectory(shared2));
  }
  guest_os::GuestOsSharePath* guest_os_share_path =
      guest_os::GuestOsSharePath::GetForProfile(browser()->profile());
  guest_os_share_path->RegisterPersistedPath(crostini::kCrostiniDefaultVmName,
                                             shared1);
  guest_os_share_path->RegisterPersistedPath(crostini::kCrostiniDefaultVmName,
                                             shared2);

  ASSERT_TRUE(RunExtensionTest("file_browser/crostini_test", {},
                               {.load_as_component = true}));
}

IN_PROC_BROWSER_TEST_F(FileManagerPrivateApiTest, CrostiniIncognito) {
  crostini::FakeCrostiniFeatures crostini_features;
  crostini_features.set_is_allowed_now(true);
  crostini_features.set_enabled(true);

  // Setup CrostiniManager for testing.
  crostini::CrostiniManager* crostini_manager =
      crostini::CrostiniManager::GetForProfile(browser()->profile());
  crostini_manager->set_skip_restart_for_testing();
  crostini_manager->AddRunningVmForTesting(crostini::kCrostiniDefaultVmName);
  crostini_manager->AddRunningContainerForTesting(
      crostini::kCrostiniDefaultVmName,
      crostini::ContainerInfo(crostini::kCrostiniDefaultContainerName,
                              "testuser", "/home/testuser", "PLACEHOLDER_IP"));

  ExpectCrostiniMount();

  scoped_refptr<extensions::FileManagerPrivateMountCrostiniFunction> function(
      new extensions::FileManagerPrivateMountCrostiniFunction());
  // Use incognito profile.
  function->SetBrowserContextForTesting(
      browser()->profile()->GetPrimaryOTRProfile(/*create_if_needed=*/true));

  extensions::api_test_utils::SendResponseHelper response_helper(
      function.get());
  function->RunWithValidation()->Execute();
  response_helper.WaitForResponse();
  EXPECT_TRUE(response_helper.GetResponse());
}

IN_PROC_BROWSER_TEST_F(FileManagerPrivateApiTest, HoldingSpace) {
  const base::FilePath test_dir = temp_dir_.GetPath();
  AddLocalFileSystem(browser()->profile(), test_dir);

  {
    base::ScopedAllowBlockingForTesting allow_io;
    base::File image_file(test_dir.Append("test_image.jpg"),
                          base::File::FLAG_CREATE | base::File::FLAG_WRITE);
    ASSERT_TRUE(image_file.IsValid());
    base::File audio_file(test_dir.Append("test_audio.mp3"),
                          base::File::FLAG_CREATE | base::File::FLAG_WRITE);
    ASSERT_TRUE(audio_file.IsValid());
    base::File video_file(test_dir.Append("test_video.mp4"),
                          base::File::FLAG_CREATE | base::File::FLAG_WRITE);
    ASSERT_TRUE(video_file.IsValid());
  }

  EXPECT_TRUE(RunExtensionTest("file_browser/holding_space", {},
                               {.load_as_component = true}));
}

IN_PROC_BROWSER_TEST_F(FileManagerPrivateApiTest, GetVolumeRoot) {
  AddLocalFileSystem(browser()->profile(), temp_dir_.GetPath());

  ASSERT_TRUE(RunExtensionTest("file_browser/get_volume_root", {},
                               {.load_as_component = true}));
}

IN_PROC_BROWSER_TEST_F(FileManagerPrivateApiTest, OpenURL) {
  const char* target_url = "https://www.google.com/";
  content::TestNavigationObserver navigation_observer(GURL{target_url});
  navigation_observer.StartWatchingNewWebContents();
  ASSERT_TRUE(RunExtensionTest("file_browser/open_url",
                               {.custom_arg = target_url},
                               {.load_as_component = true}));
  // Wait for navigation to finish.
  navigation_observer.Wait();

  // Check that the current active web contents points to the expected URL.
  BrowserList* browser_list = BrowserList::GetInstance();
  Browser* browser = browser_list->GetLastActive();
  content::WebContents* active_web_contents =
      browser->tab_strip_model()->GetActiveWebContents();
  EXPECT_STREQ(target_url,
               active_web_contents->GetVisibleURL().spec().c_str());
}

class FileManagerPrivateApiDlpTest : public FileManagerPrivateApiTest {
 public:
  FileManagerPrivateApiDlpTest() {
    scoped_feature_list_.InitAndEnableFeature(
        features::kDataLeakPreventionFilesRestriction);
  }

  FileManagerPrivateApiDlpTest(const FileManagerPrivateApiDlpTest&) = delete;
  void operator=(const FileManagerPrivateApiDlpTest&) = delete;

  ~FileManagerPrivateApiDlpTest() override = default;

  void SetUpOnMainThread() override {
    FileManagerPrivateApiTest::SetUpOnMainThread();
    ASSERT_TRUE(drive_path_.CreateUniqueTempDir());
  }

  std::unique_ptr<KeyedService> SetDlpRulesManager(
      content::BrowserContext* context) {
    auto dlp_rules_manager = std::make_unique<policy::MockDlpRulesManager>();
    mock_rules_manager_ = dlp_rules_manager.get();
    return dlp_rules_manager;
  }

 protected:
  base::ScopedTempDir drive_path_;
  policy::MockDlpRulesManager* mock_rules_manager_ = nullptr;
  base::test::ScopedFeatureList scoped_feature_list_;
};

IN_PROC_BROWSER_TEST_F(FileManagerPrivateApiDlpTest, DlpBlockCopy) {
  policy::DlpRulesManagerFactory::GetInstance()->SetTestingFactory(
      browser()->profile(),
      base::BindRepeating(&FileManagerPrivateApiDlpTest::SetDlpRulesManager,
                          base::Unretained(this)));
  ASSERT_TRUE(policy::DlpRulesManagerFactory::GetForPrimaryProfile());

  AddLocalFileSystem(browser()->profile(), temp_dir_.GetPath());

  const base::FilePath test_file_path =
      temp_dir_.GetPath().Append("dlp_test_file.txt");

  {
    base::ScopedAllowBlockingForTesting allow_io;
    base::File test_file(test_file_path,
                         base::File::FLAG_CREATE | base::File::FLAG_WRITE);
    ASSERT_TRUE(test_file.IsValid());

    ASSERT_TRUE(base::CreateDirectory(drive_path_.GetPath().Append("subdir")));
  }

  storage::ExternalMountPoints::GetSystemInstance()->RegisterFileSystem(
      "drivefs-delayed_mount_2", storage::kFileSystemTypeDriveFs,
      storage::FileSystemMountOption(), drive_path_.GetPath());
  file_manager::VolumeManager::Get(browser()->profile())
      ->AddVolumeForTesting(  // IN-TEST
          drive_path_.GetPath(), file_manager::VOLUME_TYPE_GOOGLE_DRIVE,
          chromeos::DEVICE_TYPE_UNKNOWN,
          /*read_only=*/false);

  // Set the file source in DlpClient
  base::MockCallback<chromeos::DlpClient::AddFileCallback> add_file_cb;
  EXPECT_CALL(add_file_cb, Run(testing::_));

  dlp::AddFileRequest add_file_req;
  add_file_req.set_file_path(test_file_path.value());
  add_file_req.set_source_url("example1.com");
  chromeos::DlpClient::Get()->AddFile(std::move(add_file_req),
                                      add_file_cb.Get());
  ::testing::Mock::VerifyAndClearExpectations(&add_file_cb);

  EXPECT_CALL(*mock_rules_manager_, IsRestrictedDestination(_, _, _, _, _))
      .WillOnce(::testing::Return(policy::DlpRulesManager::Level::kBlock));

  EXPECT_TRUE(RunExtensionTest("file_browser/dlp_block", {},
                               {.load_as_component = true}));
}
