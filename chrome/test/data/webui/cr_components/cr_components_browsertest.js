// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/** @fileoverview Tests for shared Polymer 3 components. */

// Polymer BrowserTest fixture.
GEN_INCLUDE(['//chrome/test/data/webui/polymer_browser_test_base.js']);

GEN('#include "chrome/browser/browser_features.h"');
GEN('#include "chrome/browser/ui/ui_features.h"');
GEN('#include "content/public/test/browser_test.h"');
GEN('#include "build/chromeos_buildflags.h"');
GEN('#include "crypto/crypto_buildflags.h"');

/* eslint-disable no-var */

/** Test fixture for shared Polymer 3 components. */
var CrComponentsBrowserTest = class extends PolymerTest {
  /** @override */
  get browsePreload() {
    return 'chrome://dummyurl';
  }

  /** @override */
  get webuiHost() {
    return 'dummyurl';
  }
};

var CrComponentsColorChangeListenerTest =
    class extends CrComponentsBrowserTest {
  /** @override */
  get browsePreload() {
    return 'chrome://test/test_loader.html?module=cr_components/color_change_listener_test.js';
  }
};

TEST_F('CrComponentsColorChangeListenerTest', 'All', function() {
  mocha.run();
});

var CrComponentsManagedFootnoteTest = class extends CrComponentsBrowserTest {
  /** @override */
  get browsePreload() {
    return 'chrome://test/test_loader.html?module=cr_components/managed_footnote_test.js';
  }
};

TEST_F('CrComponentsManagedFootnoteTest', 'All', function() {
  mocha.run();
});

GEN('#if BUILDFLAG(USE_NSS_CERTS)');

/**
 * Test fixture for chrome://settings/certificates. This tests the
 * certificate-manager component in the context of the Settings privacy page.
 */
var CrComponentsCertificateManagerTest = class extends CrComponentsBrowserTest {
  /** @override */
  get browsePreload() {
    return 'chrome://settings/test_loader.html?module=cr_components/certificate_manager_test.js';
  }
};

TEST_F('CrComponentsCertificateManagerTest', 'All', function() {
  mocha.run();
});

GEN('#endif  // BUILDFLAG(USE_NSS_CERTS)');


GEN('#if BUILDFLAG(USE_NSS_CERTS) && BUILDFLAG(IS_CHROMEOS_ASH)');

/**
 * ChromeOS specific test fixture for chrome://settings/certificates, testing
 * the certificate provisioning UI. This tests the certificate-manager component
 * in the context of the Settings privacy page.
 */
var CrComponentsCertificateManagerProvisioningTest =
    class extends CrComponentsCertificateManagerTest {
  /** @override */
  get browsePreload() {
    return 'chrome://settings/test_loader.html?module=cr_components/certificate_manager_provisioning_test.js';
  }
};

TEST_F('CrComponentsCertificateManagerProvisioningTest', 'All', function() {
  mocha.run();
});

GEN('#endif  // BUILDFLAG(USE_NSS_CERTS) && BUILDFLAG(IS_CHROMEOS_ASH)');

var CrComponentsManagedDialogTest = class extends CrComponentsBrowserTest {
  /** @override */
  get browsePreload() {
    return 'chrome://test/test_loader.html?module=cr_components/managed_dialog_test.js';
  }
};

TEST_F('CrComponentsManagedDialogTest', 'All', function() {
  mocha.run();
});
