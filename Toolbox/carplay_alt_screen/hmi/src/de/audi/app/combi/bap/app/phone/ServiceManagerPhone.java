package de.audi.app.combi.bap.app.phone;

import com.luka.carplay.telephony.GatedPhoneListener;
import com.luka.carplay.telephony.GatedAddressBook;
import com.luka.carplay.telephony.GatedPhoneService;
import de.audi.app.bap.fw.AbstractBAPModuleServiceManager;
import de.audi.app.combi.bap.fw.AbstractCombiModule;
import de.audi.atip.activator.AbstractActivator;
import de.audi.atip.base.AppNameConst;
import de.audi.atip.interapp.combi.bap.phone.CombiBAPServiceAddressBookListener;
import de.audi.atip.interapp.combi.bap.phone.CombiBAPServiceMessagingListener;
import de.audi.atip.interapp.combi.bap.phone.CombiBAPServicePhoneListener;
import de.audi.atip.log.LogChannel;
import org.osgi.framework.BundleContext;
import org.osgi.framework.ServiceReference;
import org.osgi.util.tracker.ServiceTracker;

/* Patched (from LuKa's mib2q-carplay-rgi, GPL-3.0): CombiBAPServicePhone is registered
 * through GatedPhoneService, the address book through GatedAddressBook, and the phone
 * listener is wrapped in GatedPhoneListener, so CarPlay calls reach the cluster
 * telephone tab and its actions reach CarPlay.  Otherwise identical to stock, which is
 * byte-identical on MU1316 (US), MU1329 (EU) and MU1003 (CN). */
public class ServiceManagerPhone extends AbstractBAPModuleServiceManager {
    private final LogChannel logChannel;

    public ServiceManagerPhone(AbstractCombiModule abstractcombimodule, BundleContext bundlecontext) {
        super(abstractcombimodule, bundlecontext);
        this.logChannel = abstractcombimodule.getLogChannel();
    }

    public Object addingService(ServiceReference servicereference) {
        Object object = this.bundleContext.getService(servicereference);
        if (object instanceof CombiBAPServicePhoneListener) {
            this.logChannel.log(10000000, "[ServiceManagerPhone#addingService] CombiBAPServicePhoneListener found");
            ((AppConnectorPhone)this.module.getAppConnectors().get("Phone"))
                .setAppServiceListener(gatedListener((CombiBAPServicePhoneListener)object));
            return object;
        } else if (object instanceof CombiBAPServiceAddressBookListener) {
            this.logChannel
                .log(10000000, "[ServiceManagerPhone#addingService] CombiBAPServiceAddressBookListener found");
            ((AppConnectorAddressBook)this.module.getAppConnectors().get(AppNameConst.APP_NAME_ADDRESSBOOK))
                .setAppServiceListener((CombiBAPServiceAddressBookListener)object);
            return object;
        } else if (object instanceof CombiBAPServiceMessagingListener) {
            this.logChannel.log(10000000, "[ServiceManagerPhone#addingService] CombiBAPServiceMessagingListener found");
            ((AppConnectorMessaging)this.module.getAppConnectors().get(AppNameConst.APP_NAME_MESSAGING))
                .setAppServiceListener((CombiBAPServiceMessagingListener)object);
            return object;
        } else {
            this.bundleContext.ungetService(servicereference);
            return super.addingService(servicereference);
        }
    }

    public void removedService(ServiceReference servicereference, Object object) {
        if (object instanceof CombiBAPServicePhoneListener) {
            this.logChannel.log(10000000, "[ServiceManagerPhone#removedService] CombiBAPServicePhoneListener removed");
            ((AppConnectorPhone)this.module.getAppConnectors().get("Phone")).setAppServiceListener(null);
            this.bundleContext.ungetService(servicereference);
        } else if (object instanceof CombiBAPServiceAddressBookListener) {
            this.logChannel
                .log(10000000, "[ServiceManagerPhone#removedService] CombiBAPServiceAddressBookListener removed");
            ((AppConnectorAddressBook)this.module.getAppConnectors().get(AppNameConst.APP_NAME_ADDRESSBOOK))
                .setAppServiceListener(null);
            this.bundleContext.ungetService(servicereference);
        } else if (object instanceof CombiBAPServiceMessagingListener) {
            this.logChannel
                .log(10000000, "[ServiceManagerPhone#removedService] CombiBAPServiceMessagingListener removed");
            ((AppConnectorMessaging)this.module.getAppConnectors().get(AppNameConst.APP_NAME_MESSAGING))
                .setAppServiceListener(null);
            this.bundleContext.ungetService(servicereference);
        } else {
            super.removedService(servicereference, object);
        }
    }

    public void registerServices(AbstractActivator abstractactivator) {
        this.logChannel
            .log(
                10000000,
                "[ServiceManagerPhone#registerServices] Registering AppConnectorPhone as CombiBAPServicePhone"
            );
        abstractactivator.registerService(
            "de.audi.atip.interapp.combi.bap.phone.CombiBAPServicePhone",
            gatedPhone(this.module.getAppConnectors().get("Phone")),
            null
        );
        this.logChannel
            .log(
                10000000,
                "[ServiceManagerPhone#registerServices] Registering AppConnectorAddressBook as CombiBAPServiceAddressBook"
            );
        abstractactivator.registerService(
            "de.audi.atip.interapp.combi.bap.phone.CombiBAPServiceAddressBook",
            gatedAddressBook(this.module.getAppConnectors().get(AppNameConst.APP_NAME_ADDRESSBOOK)),
            null
        );
        this.logChannel
            .log(
                10000000,
                "[ServiceManagerPhone#registerServices] Registering AppConnectorMessaging as CombiBAPServiceMessaging"
            );
        abstractactivator.registerService(
            "de.audi.atip.interapp.combi.bap.phone.CombiBAPServiceMessaging",
            this.module.getAppConnectors().get(AppNameConst.APP_NAME_MESSAGING),
            null
        );
        this.logChannel
            .log(
                10000000,
                "[ServiceManagerPhone#registerServices] Registering AppConnectorConnectivity as CombiBAPServiceConnectivity"
            );
        abstractactivator.registerService(
            "de.audi.atip.interapp.combi.bap.phone.CombiBAPServiceConnectivity",
            this.module.getAppConnectors().get(AppNameConst.APP_NAME_CONNECTIVITY),
            null
        );
        this.logChannel
            .log(
                10000000,
                "[ServiceManagerPhone#registerServices] Registering InitializationManagerPhone as MessageListener"
            );
        abstractactivator.registerService(
            "de.audi.atip.msg.MsgListener",
            this.module.getInitializationManager(),
            null
        );
    }

    /* The telephony wrappers.  If our classes cannot load or wrap, the stock objects
     * are registered exactly as stock does, so the BT phone tab never depends on them. */
    private CombiBAPServicePhoneListener gatedListener(CombiBAPServicePhoneListener listener) {
        try {
            return GatedPhoneListener.wrap(listener);
        } catch (Throwable t) {
            this.logChannel.log(10000000, "[ServiceManagerPhone] telephony listener wrapper unavailable");
            return listener;
        }
    }

    private Object gatedPhone(Object service) {
        try {
            return GatedPhoneService.wrap(service);
        } catch (Throwable t) {
            this.logChannel.log(10000000, "[ServiceManagerPhone] telephony service wrapper unavailable");
            return service;
        }
    }

    private Object gatedAddressBook(Object service) {
        try {
            return GatedAddressBook.wrap(service);
        } catch (Throwable t) {
            this.logChannel.log(10000000, "[ServiceManagerPhone] address book wrapper unavailable");
            return service;
        }
    }

    public void trackServices() {
        String[] astring = new String[]{
            "de.audi.atip.interapp.combi.bap.phone.CombiBAPServicePhoneListener",
            "de.audi.atip.interapp.combi.bap.phone.CombiBAPServiceAddressBookListener",
            "de.audi.atip.diag.sw.SwDiagnosisManager"
        };
        this.serviceTracker = new ServiceTracker(this.bundleContext, astring, this);
        this.serviceTracker.open();
    }
}
