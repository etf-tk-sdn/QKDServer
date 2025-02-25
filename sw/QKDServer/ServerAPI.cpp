#define LOADTESTING

#include "ServerAPI.h"
#include "base64.h"
#include "combaseapi.h"

QKD::AuthDB* QKD::ServerAPI::GetAuthDB() {
    return _authDB;
}

QKD::StatusDB* QKD::ServerAPI::GetStatusDB() {
    return _statusDB;
}

QKD::KeysDB* QKD::ServerAPI::GetKeysDB() {
    return _keysDB;
}

void QKD::ServerAPI::SetAuthDB(QKD::AuthDB* authDB) {
    _authDB = authDB;
}

void QKD::ServerAPI::SetStatusDB(QKD::StatusDB* statusDB) {
    _statusDB = statusDB;
}

void QKD::ServerAPI::SetKeysDB(QKD::KeysDB* keysDB) {
    _keysDB = keysDB;
}

std::string QKD::ServerAPI::GetSAEID(std::string publicKey)
{
	std::string SAE_ID = "";
    QKD::ServerAPI::GetAuthDB()->GetAuthValue(publicKey, SAE_ID);
    return SAE_ID;
}

int QKD::ServerAPI::GetStatus(std::string SlaveSAEID, std::string callerSAEId, Status** Status)
{
    StatusEntry* e1;
    if (ServerAPI::GetInstance().GetStatusDB()->GetStatusEntry(SlaveSAEID,&e1)) {

        if (e1->status.master_SAE_ID != callerSAEId)
        {
            return RESPONSE_UNAUTHORIZED;
        }
        else
        {
            *Status = (&e1->status);
            return RESPONSE_OK;
        }
    }
    else
        return RESPONSE_NOT_FOUND;
}

int QKD::ServerAPI::GetKeys(std::string callerSAEId, GetKeysRequest getKeyRequest, KeyContainer** keyContainer)
{
  
    std::scoped_lock locker(_server_lock);
    Status status;
    StatusEntry* e1;
    if (ServerAPI::GetInstance().GetStatusDB()->GetStatusEntry(getKeyRequest.slave_SAE_ID, &e1))
    {
        if (e1->status.master_SAE_ID != callerSAEId)
        {
            return RESPONSE_UNAUTHORIZED;
        }
        else
        {
            if (getKeyRequest.size == 0) getKeyRequest.size = (e1->status).key_size;
            if (getKeyRequest.size % 8 != 0)
            {
                return RESPONSE_SIZE_SHALL_BE_MULTIPLE_OF_8;
            }
            else if (getKeyRequest.size * getKeyRequest.number > (unsigned long int)((e1->rawKeys).size()) * 8ul) //Provjera da li ima dovoljno materijala za trazene kljuceve
            {
                std::cout << "getKeyRequest.size = " << getKeyRequest.size << std::endl;
                std::cout << "getKeyRequest.number = " << getKeyRequest.number << std::endl;
                std::cout << "(e1->rawKeys).size() = " << (unsigned long int)(e1->rawKeys).size() << std::endl;
                std::cout << "------------------" << std::endl;
                return RESPONSE_NOT_ENOUGH_MATERIAL;
            }
            else
            {
                KeyContainer* keyContainer1 = new KeyContainer;
 
                for (unsigned long int z = 0; z < getKeyRequest.number; z++)  //Proces kreiranja kljuca, kodiranja i generisanja njegovog ID-a ponavlja se onoliko puta koliko je zahtjevano kljuceva
                {
                    uint8_t* key = new uint8_t[getKeyRequest.size / 8];
                    for (unsigned long int i = 0; i < getKeyRequest.size / 8; i++)  //Kreiranje pojedinacnih kljuceva zadane velicine
                    {
                        //key.push_back(e1->rawKeys.front());
                        key[i] = e1->rawKeys.front();
                        e1->rawKeys.pop(); //Brisanje preuzetih kljuceva iz baze, a automatski se azurira i status!!!
#ifdef LOADTESTING          
                        e1->rawKeys.push(key[i]);
#endif
                    }

                    //Generisanje Key ID-a za prethodno kreirani kljuc
                    GUID guid = {0};
                    char szGuid[36] = {0};
                    CoCreateGuid(&guid);
                    sprintf(szGuid, "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", guid.Data1, guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
                    std::string key_ID = "";
                    for (int j = 0; j < (sizeof(szGuid) / sizeof(char)); j++) {
                        key_ID = key_ID + szGuid[j];
                    }

                    //Base64 encoding kljuca
                    std::string encodedData = base64_encode(key, getKeyRequest.size / 8);
                    delete[] key;

                    //keyContainer1 je novi key+key ID koji ce se ispisati
                    keyContainer1->keys.push_back(Key(key_ID, encodedData));
#ifndef LOADTESTING
                    ServerAPI::GetInstance().GetKeysDB()->PutKeyContainerValue(std::make_pair(getKeyRequest.slave_SAE_ID, key_ID), std::make_pair((e1->status).master_SAE_ID, encodedData));
                    
                    if ((getKeyRequest.additional_slave_IDs).size() != 0) //Ukoliko su definisani i dodatni ID-evi, upisuje kreirane kljuceve i za njih
                    {
                        for (int j = 0; j < (getKeyRequest.additional_slave_IDs).size(); j++)
                        {
                        ServerAPI::GetInstance().GetKeysDB()->PutKeyContainerValue(std::make_pair((getKeyRequest.additional_slave_IDs)[j], key_ID), std::make_pair(callerSAEId, encodedData));
                        }
                    }
#endif
                    /* while (!key.empty()) { //Praznjenje kljuca kako nove vrijednosti ne bi uticale na novi kljuc
                        key.pop_back();
                    }*/
                }

                *keyContainer = &(*keyContainer1);
                return RESPONSE_OK;
            }
        }
    }
    else
        return RESPONSE_NOT_FOUND; 

}

int QKD::ServerAPI::GetKeysWithId(std::string callerSAEId, GetKeysWithIDRequest getKeyWithIDRequest, KeyContainer** keyContainer)
{
    std::scoped_lock locker(_server_lock);
    std::pair<std::string, std::string> key_MasterID;
    KeyContainer* keyContainer1 = new KeyContainer;

    for (int j = 0; j < getKeyWithIDRequest.key_IDs.size(); j++) {
        if (ServerAPI::GetInstance().GetKeysDB()->GetKeyConteinerValue(std::make_pair(callerSAEId, getKeyWithIDRequest.key_IDs[j]), key_MasterID))
        {
            if (key_MasterID.first != getKeyWithIDRequest.master_SAE_ID)
            {
                return RESPONSE_UNAUTHORIZED;
            }
            else
            {
                keyContainer1->keys.push_back(Key(getKeyWithIDRequest.key_IDs[j], key_MasterID.second));
            }
        }
        else //ukoliko uopce nema trazenog ID-a u bazi
        {
            return RESPONSE_NOT_FOUND;
        }
    }
    for (int j = 0; j<int((keyContainer1->keys).size()); j++) {
        ServerAPI::GetInstance().GetKeysDB()->DeleteKeyContainerValue(std::make_pair(callerSAEId, getKeyWithIDRequest.key_IDs[j]), key_MasterID);
    }

    *keyContainer = &(*keyContainer1);
    return RESPONSE_OK;
}

