-- ============================================================
-- ThermaSafe — ترقية: إحداثيات موقع نداء SOS (خط الطول/العرض)
-- شغّله مرة واحدة في Supabase → SQL Editor → Run
-- إضافي وآمن: لا يحذف بيانات.
-- ============================================================
alter table public.sos_alerts add column if not exists lat numeric;
alter table public.sos_alerts add column if not exists lng numeric;
