# Cutting new keys:

py -m pip install cryptography
py scripts\admin\sealblocks.py root-key .\lfkeys\root
**.\lfkeys\root.key: keep it offline. .\lfkeys\root.pub: public key 795ce82e641823f93bbd54912e9be3190065c5925c427f896d30a3058de1d3ae**
py scripts\admin\sealblocks.py release-key .\lfkeys\root.key 1 .\lfkeys\release1.key --product 0x4C46
**.\lfkeys\release1.key: release key serial 1 for product 0x4c46**
py scripts\admin\sealblocks.py root-header .\lfkeys\root.pub --product 0x4C46 --force -o src\target\CH32H417\V5F\User\sealed_root.h
**src\target\CH32H417\V5F\User\sealed_root.h: root key 795ce82e641823f9..., product 0x4c46**

Clean and rebuild in MounRiver, copy `LiteForth_V5F.bin` to the test folder.

py ..\scripts\admin\sealblocks.py package LiteForth_V5F.bin app.lfu --component app --pages lfpages.bin --release ..\lfkeys\release1.key --boards boards.csv --revision 10
