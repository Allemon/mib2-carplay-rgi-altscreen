#!/usr/bin/env python3
"""Adapt java_patch/ to an older HMI software train at build time.

java_patch/ overrides whole stock HMI classes and is written against MU1316 / MU1329.  Older
trains differ in a few stock APIs; instead of a second source tree, each variant is a list of
exact text edits applied to copies of the affected files (scripts/build_java.sh compiles the
copies in place of the originals).  Every edit must match exactly once, so a source change that
moves an anchor fails the build instead of silently producing a wrong variant.

  mu1003   MHI2Q_CN_AUG22_K1004 (lsd.jxe MU1003, 2019):
           - no IDSISmartphoneManager.ISmartphoneProperties: ExternalEventsListener and
             HighPriorityResourceTracker take no smartphone-properties argument and publish no
             MU HFP-call / RVC properties
           - no ecall EmergencyNumber API (HighPriorityResourceTracker's ecall state)
           - IDSIResource getters take no "deactivate" flag; stock MU1003 sends transfer
             priority 2/1 and unborrow constraint 0 for that case itself
           - AbstractPartialPopupManager is still called PartialPopupManager
           - DisplayManagerMIB2High has one more stock context, dc[34] = {16, 50, 51}
           - ClusterService has no satellite-map provider flag: nothing clears it there,
             so online navigation would always be reported off; use stock MU1003's rule

  tools/java_variant.py <variant> <java_patch dir> <out dir>    # writes only the edited files
  tools/java_variant.py --detect <stock jar>                     # prints the variant or "none"
"""
import os
import sys
import zipfile

EEL = "de/audi/app/terminalmode/ExternalEventsListener.java"
HPRT = "de/audi/app/terminalmode/interapp/HighPriorityResourceTracker.java"
CDLC = "de/audi/app/terminalmode/dsi/carplay/CarplayDSILifecycleController.java"
PPM = "de/esolutions/hmi/widgets/audi/evo/high/PartialPopupManagerEvoHigh.java"
DMH = "de/audi/tghu/fwhmi/DisplayManagerMIB2High.java"
CS = "de/audi/tghu/navi/app/cluster/ClusterService.java"

VARIANTS = {
    "mu1003": {
        EEL: [
            ("import de.audi.app.terminalmode.smartphone.IDSISmartphoneManager;\n", ""),
            ("    private IDSISmartphoneManager.ISmartphoneProperties smartphoneProperties;\n", ""),
            ("        IDispatcher dispatcher,\n"
             "        IDSISmartphoneManager.ISmartphoneProperties smartphoneProperties\n"
             "    ) {",
             "        IDispatcher dispatcher\n"
             "    ) {"),
            ("        this.smartphoneProperties = smartphoneProperties;\n", ""),
            ("                    ExternalEventsListener.this.smartphoneProperties\n"
             "                        .getPropertyMUHFPPhonecallActive()\n"
             "                        .accept(new Boolean(telState.getCallActive()));\n", ""),
        ],
        HPRT: [
            ("import de.audi.app.terminalmode.smartphone.IDSISmartphoneManager;\n", ""),
            ("import de.audi.atip.interapp.bap.ecall.data.EmergencyNumber;\n", ""),
            ("    private final IDSISmartphoneManager.ISmartphoneProperties smartphoneProperties;\n", ""),
            ("        PropertyFactory propertyfactory,\n"
             "        IDSISmartphoneManager.ISmartphoneProperties idsismartphonemanager$ismartphoneproperties\n"
             "    ) {",
             "        PropertyFactory propertyfactory\n"
             "    ) {"),
            ("        this.smartphoneProperties = idsismartphonemanager$ismartphoneproperties;\n", ""),
            ("                    this.smartphoneProperties.getPropertyMURVCActive().accept(Boolean.FALSE);\n", ""),
            ("            this.smartphoneProperties.getPropertyMURVCActive().accept(new Boolean(false));\n", ""),
            # MU1003 stock only blocks the screen for CarPlay devices here.
            ("            this.rvcActive.accept(new Boolean(true));\n"
             "        } else {\n"
             "            this.context\n"
             "                .getChoiceModel(ICoreTerminalModeModelBank.SMARTPHONE_SCREEN_BLOCKED_CHOICE)\n"
             "                .setValue(this.context.getDeviceManager().getActiveDevice().isCarplayDevice() ? 3 : 4);\n"
             "            this.rvcActive.accept(new Boolean(true));\n"
             "        }\n"
             "\n"
             "        this.smartphoneProperties.getPropertyMURVCActive().accept(new Boolean(true));\n",
             "            this.rvcActive.accept(new Boolean(true));\n"
             "        }\n"),
            ("\n"
             "        public boolean isLowPrioritySOSEmergencyCallType() {\n"
             "            return false;\n"
             "        }\n"
             "\n"
             "        public EmergencyNumber[] getAllowedEmergencyNumbers() {\n"
             "            return new EmergencyNumber[0];\n"
             "        }\n"
             "\n"
             "        public String getEmergencyNumberToBeDialed() {\n"
             "            return null;\n"
             "        }\n", ""),
        ],
        CDLC: [
            ("                int j = aidsiresource[i].getDSITakeType(flag);",
             "                int j = aidsiresource[i].getDSITakeType();"),
            ("                        aidsiresource[i].getDSITransferPriority(flag),\n"
             "                        aidsiresource[i].getDSITakeConstraint(flag),\n"
             "                        aidsiresource[i].getDSIBorrowConstraint(flag),\n"
             "                        aidsiresource[i].getDSIUnborrowConstraint(flag)\n",
             "                        flag ? 2 : 1,\n"
             "                        aidsiresource[i].getDSITakeConstraint(),\n"
             "                        aidsiresource[i].getDSIBorrowConstraint(),\n"
             "                        flag ? 0 : aidsiresource[i].getDSIUnborrowConstraint()\n"),
        ],
        DMH: [
            ("        this.dc[32] = new DisplayContext(32, new int[]{16, 21});\n",
             "        this.dc[32] = new DisplayContext(32, new int[]{16, 21});\n"
             "        this.dc[34] = new DisplayContext(34, new int[]{16, 50, 51});   // MU1003 stock\n"),
        ],
        CS: [
            ("setOnlineNavigationState(i == 1 && !this.hasSatMapProviderChanged(), j, true);",
             "setOnlineNavigationState(i == 1, j, true);"),
        ],
        PPM: [
            ("import de.esolutions.hmi.widgets.audi.base.AbstractPartialPopupManager;",
             "import de.esolutions.hmi.widgets.audi.base.PartialPopupManager;"),
            ("public class PartialPopupManagerEvoHigh extends AbstractPartialPopupManager implements",
             "public class PartialPopupManagerEvoHigh extends PartialPopupManager implements"),
        ],
    },
}


def detect(jar):
    names = set(zipfile.ZipFile(jar).namelist())
    base = "de/esolutions/hmi/widgets/audi/base/"
    if base + "AbstractPartialPopupManager.class" in names:
        return "none"
    if base + "PartialPopupManager.class" in names:
        return "mu1003"
    return "unknown"


def apply(variant, src, out):
    if variant not in VARIANTS:
        sys.exit("unknown variant %s (known: %s)" % (variant, ", ".join(sorted(VARIANTS))))
    for rel, edits in sorted(VARIANTS[variant].items()):
        text = open(os.path.join(src, rel), encoding="utf-8").read()
        for old, new in edits:
            n = text.count(old)
            if n != 1:
                sys.exit("%s: variant %s anchor found %d times (expected 1):\n%s" % (rel, variant, n, old))
            text = text.replace(old, new)
        dst = os.path.join(out, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        open(dst, "w", encoding="utf-8").write(text)
        print("  variant %s: %s (%d edits)" % (variant, rel, len(edits)))


def main(argv):
    if len(argv) == 3 and argv[1] == "--detect":
        print(detect(argv[2]))
        return 0
    if len(argv) != 4:
        print("usage: java_variant.py <variant> <java_patch dir> <out dir> | --detect <stock jar>", file=sys.stderr)
        return 2
    apply(argv[1], argv[2], argv[3])
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
