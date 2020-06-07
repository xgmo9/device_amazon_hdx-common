#!/system/bin/sh

# Trigger provisioning if device has not been provisioned
if [ ! -f /persist/provisioning_ok ]; then
	/system/bin/sh /persist/prov.sh
fi
