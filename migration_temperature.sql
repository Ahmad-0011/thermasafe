-- ============================================================
-- ThermaSafe — ترقية: قياس حرارة العامل (DHT11) + حدّ تحذير للمختص
-- شغّله مرة واحدة في: Supabase Dashboard → SQL Editor → New query → Run
-- إضافي وآمن: لا يحذف أي بيانات موجودة.
-- ============================================================

-- 1) أعمدة الحرارة في جدول العمال
alter table public.workers add column if not exists temperature numeric;
alter table public.workers add column if not exists humidity    numeric;
alter table public.workers add column if not exists temp_at      timestamptz;

-- 2) جدول الإعدادات (حدّ الحرارة) — صف واحد فقط
create table if not exists public.settings (
  id             int primary key default 1,
  temp_threshold numeric not null default 38,
  constraint settings_single_row check (id = 1)
);
insert into public.settings(id, temp_threshold) values (1, 38)
  on conflict (id) do nothing;

alter table public.settings enable row level security;
drop policy if exists settings_read on public.settings;
create policy settings_read on public.settings for select using (true);

-- 3) دالة تحديث حدّ الحرارة (يستخدمها المختص)
create or replace function public.ts_set_threshold(p_val numeric)
returns void language sql security definer set search_path = public as $$
  update public.settings set temp_threshold = p_val where id = 1;
$$;

-- 4) دالة تحديث حرارة العامل (يستخدمها ESP + حساس DHT11) — بالرقم التسلسلي
create or replace function public.ts_update_temp(p_serial text, p_temp numeric, p_hum numeric)
returns void language plpgsql security definer set search_path = public as $$
begin
  update public.workers
     set temperature = p_temp, humidity = p_hum, temp_at = now()
   where serial = p_serial;
  if not found then
    insert into public.workers(name, serial, temperature, humidity, temp_at, day, week, month, status)
    values ('عامل ' || p_serial, p_serial, p_temp, p_hum, now(), 'safe','safe','safe','safe');
  end if;
end;
$$;

grant execute on function public.ts_set_threshold(numeric)              to anon, authenticated;
grant execute on function public.ts_update_temp(text,numeric,numeric)   to anon, authenticated;

-- 5) المزامنة اللحظية لجدول الإعدادات (محصّن ضد التكرار)
do $$
begin
  if not exists (select 1 from pg_publication_tables
                 where pubname='supabase_realtime' and schemaname='public' and tablename='settings') then
    alter publication supabase_realtime add table public.settings;
  end if;
end $$;

-- ============================================================
-- تم. الآن ESP يرسل الحرارة عبر ts_update_temp، والمختص يضبط الحد عبر التطبيق.
-- ============================================================
