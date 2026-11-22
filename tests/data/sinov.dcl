// AsiaCAD DCL sinov dialogi (sintetik)
@include "acad.dcl"
qator : edit_box { edit_width = 8; }
sinov : dialog {
  label = "Aylana";
  : boxed_column { label = "Parametrlar";
    : qator { key = "r"; label = "Radius:"; value = "5"; }
    : popup_list { key = "qat"; label = "Qatlam:"; }
    : toggle { key = "mark"; label = "Belgi"; value = "1"; }
    : radio_row { key = "tur";
      : radio_button { key = "t1"; label = "Bir"; value = "1"; }
      : radio_button { key = "t2"; label = "Ikki"; }
    }
  }
  ok_cancel;
}
