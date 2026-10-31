/*
 * Patched de.audi.tghu.phone.sm.Activator: identical to stock except that it builds
 * CarPlayPhoneSMM, which skips the HU "Your iPhone is in Apple CarPlay mode" popup.
 * Stock is byte-identical on MU1316, MU1329 and MU1003.  If CarPlayPhoneSMM cannot
 * be loaded, the stock PhoneSMM is built instead, so the phone app always starts.
 *
 * Copyright (c) 2026 LuKa (@LuKa_dev)
 */
package de.audi.tghu.phone.sm;

import com.luka.carplay.telephony.CarPlayPhoneSMM;
import de.audi.atip.activator.AbstractSMMActivator;

public class Activator extends AbstractSMMActivator {
    public void init() {
        this.smmList = new PhoneSMM[8];
        if (this.framework.isFrontMU()) {
            this.smmList[0] = front(0, "main");
            if (this.framework.isDualView()) {
                this.smmList[2] = front(2, "frontPassenger");
            }
        }
        this.reqActionProxies = PhoneSMMActions.REQUIRED_ACTION_PROXY_MODULE_IDS;
    }

    /* Typed Object, so verifying this class never has to load CarPlayPhoneSMM. */
    private Object carPlay(int n, String name) {
        return new CarPlayPhoneSMM(this.framework, n, name);
    }

    private PhoneSMM front(int n, String name) {
        try {
            return (PhoneSMM)carPlay(n, name);
        } catch (LinkageError e) {
            return new PhoneSMM(this.framework, n, name);
        }
    }
}
