#pragma once
#include <string>
#include <map>
#include <iostream>

// Mapping of İTÜ OBS Result Codes to Messages
const std::map<std::string, std::string> RESULT_MESSAGES = {
    {"successResult", "CRN {} için işlem başarıyla tamamlandı."},
    {"errorResult", "CRN {} için Operasyon tamamlanamadı."},
    {"None", "CRN {} için Operasyon tamamlanamadı."},
    {"error", "CRN {} için bir hata meydana geldi."},
    {"VAL01", "CRN {} bir problemden dolayı alınamadı."},
    {"VAL02", "CRN {} kayıt zaman engelinden dolayı alınamadı."},
    {"VAL03", "CRN {} bu dönem zaten alındığından dolayı tekrar alınamadı."},
    {"VAL04", "CRN {} ders planında yer almadığından dolayı alınamadı."},
    {"VAL05", "CRN {} dönemlik maksimum kredi sınırını aştığından dolayı alınamadı."},
    {"VAL06", "CRN {} kontenjan yetersizliğinden dolayı alınamadı."},
    {"VAL07", "CRN {} daha önce AA notuyla verildiğinden dolayı alınamadı."},
    {"VAL08", "CRN {} program şartını sağlamadığından dolayı alınamadı."},
    {"VAL09", "CRN {} başka bir dersle çakıştığından dolayı alınamadı."},
    {"VAL10", "CRN {} dersine kayıtlı olmadığınızdan dolayı hiç bir işlem yapılmadı."},
    {"VAL11", "CRN {} önşartlardan dolayı alınamadı."},
    {"VAL12", "CRN {} şu anki dönemde açılmadığından dolayı alınamadı."},
    {"VAL13", "CRN {} geçici olarak engellenmiş olması sebebiyle alınamadı."},
    {"VAL14", "Sistem geçici olarak yanıt vermiyor."},
    {"VAL15", "Maksimum 12 CRN alabilirsiniz."},
    {"VAL16", "Aktif bir işleminiz devam ettiğinden dolayı işlem yapılamadı."},
    {"VAL18", "CRN {} engellendğinden dolayı alınamadı."},
    {"VAL19", "CRN {} önlisans dersi olduğundan dolayı alınamadı."},
    {"VAL20", "Dönem başına sadece 1 ders bırakabilirsiniz."},
    {"VAL21", "İşlem sırasında bir hata oluştu."},
    {"CRNListEmpty", "CRN {} listesi boş göründüğünden alınamadı."},
    {"CRNNotFound", "CRN {} bulunamadığından dolayı alınamadı."},
    {"ERRLoad", "Sistem geçici olarak yanıt vermiyor."},
    {"NULLParam-CheckOgrenciKayitZamaniKontrolu", "CRN {} kayıt zaman engelinden dolayı alınamadı."},
    {"Ekleme İşlemi Başarılı", "CRN {} için ekleme işlemi başarıyla tamamlandı."},
    {"Kontenjan Dolu", "CRN {} için kontenjan dolu olduğundan dolayı alınamadı."},
    {"Silme İşlemi Başarılı", "CRN {} için silme işlemi başarıyla tamamlandı."}
};

// Helper function to fetch message and insert CRN into the "{}" placeholder
inline std::string get_result_message(const std::string& code, const std::string& crn) {
    auto it = RESULT_MESSAGES.find(code);
    std::string msg;

    if (it != RESULT_MESSAGES.end()) {
        msg = it->second;
    } else {
        // Fallback for codes not in the list
        msg = "Bilinmeyen Kod: " + code + " (CRN {})";
    }

    // Find the placeholder "{}" and replace it with the actual CRN
    size_t placeholder_pos = msg.find("{}");
    if (placeholder_pos != std::string::npos) {
        msg.replace(placeholder_pos, 2, crn);
    }

    return msg;
}