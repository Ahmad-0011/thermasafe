-- ============================================================
-- ThermaSafe — مخطط قاعدة بيانات Supabase (PostgreSQL)
-- انسخ هذا الملف كاملاً والصقه في:  Supabase Dashboard → SQL Editor → New query → Run
-- آمن لإعادة التشغيل (idempotent).
-- ============================================================

-- ---------- تنظيف أي جداول قديمة (آمن: لا توجد بيانات حقيقية بعد) ----------
drop table if exists public.sos_alerts cascade;
drop table if exists public.accounts  cascade;
drop table if exists public.workers   cascade;
drop table if exists public.settings  cascade;

-- ---------- الجداول ----------
create table if not exists public.workers (
  id          bigint generated always as identity primary key,
  name        text not null,
  serial      text unique not null,
  day         text not null default 'safe',
  week        text not null default 'safe',
  month       text not null default 'safe',
  status      text not null default 'safe',
  temperature numeric,
  humidity    numeric,
  temp_at     timestamptz,
  created_at  timestamptz not null default now()
);

create table if not exists public.settings (
  id             int primary key default 1,
  temp_threshold numeric not null default 38,
  constraint settings_single_row check (id = 1)
);

create table if not exists public.accounts (
  id       bigint generated always as identity primary key,
  username text unique not null,
  password text not null,                       -- ديمو فقط (انظر ملاحظة الأمان في README)
  role     text not null check (role in ('worker','specialist')),
  name     text not null,
  serial   text
);

create table if not exists public.sos_alerts (
  id         bigint generated always as identity primary key,
  serial     text,
  name       text,
  location   text,
  created_at timestamptz not null default now()
);

-- ---------- تفعيل حماية الصفوف (RLS) ----------
alter table public.workers    enable row level security;
alter table public.accounts   enable row level security;
alter table public.sos_alerts enable row level security;
alter table public.settings   enable row level security;

-- workers: القراءة متاحة للجميع (التعديل يتم فقط عبر الدوال الآمنة أدناه)
drop policy if exists workers_read on public.workers;
create policy workers_read on public.workers for select using (true);

-- settings: القراءة متاحة للجميع (التعديل عبر الدالة الآمنة)
drop policy if exists settings_read on public.settings;
create policy settings_read on public.settings for select using (true);

-- sos_alerts: القراءة والإضافة متاحة (عامل يرسل نداء، مختص يقرأه)
drop policy if exists sos_read   on public.sos_alerts;
drop policy if exists sos_insert on public.sos_alerts;
create policy sos_read   on public.sos_alerts for select using (true);
create policy sos_insert on public.sos_alerts for insert with check (true);

-- accounts: لا توجد أي سياسة وصول مباشر → كلمات المرور لا تُقرأ من الواجهة إطلاقاً.
-- كل عمليات الحسابات تتم عبر الدوال الآمنة (security definer) التالية فقط.

-- ---------- الدوال الآمنة (RPC) ----------
create or replace function public.ts_login(p_user text, p_pass text)
returns table(username text, role text, name text, serial text)
language sql security definer set search_path = public as $$
  select username, role, name, serial
  from public.accounts
  where username = p_user and password = p_pass
  limit 1;
$$;

create or replace function public.ts_signup(
  p_user text, p_pass text, p_role text, p_name text, p_serial text
) returns table(username text, role text, name text, serial text)
language plpgsql security definer set search_path = public as $$
declare v_serial text;
begin
  if exists (select 1 from public.accounts where username = p_user) then
    raise exception 'USER_EXISTS';
  end if;
  v_serial := coalesce(nullif(p_serial, ''),
                       (case when p_role = 'worker' then 'TH-' else 'SF-' end)
                       || floor(random()*90000 + 10000)::int::text);
  insert into public.accounts(username, password, role, name, serial)
  values (p_user, p_pass, p_role, p_name, v_serial);
  if p_role = 'worker' and not exists (select 1 from public.workers where serial = v_serial) then
    insert into public.workers(name, serial, day, week, month, status)
    values (p_name, v_serial, 'safe', 'safe', 'safe', 'safe');
  end if;
  return query select p_user, p_role, p_name, v_serial;
end;
$$;

create or replace function public.ts_add_worker(
  p_name text, p_serial text, p_level text, p_account boolean
) returns void
language plpgsql security definer set search_path = public as $$
declare v_serial text;
begin
  v_serial := coalesce(nullif(p_serial, ''), 'TH-' || floor(random()*90000 + 10000)::int::text);
  insert into public.workers(name, serial, day, week, month, status)
  values (p_name, v_serial, p_level, p_level, p_level, p_level)
  on conflict (serial) do nothing;
  if p_account and not exists (select 1 from public.accounts where username = lower(v_serial)) then
    insert into public.accounts(username, password, role, name, serial)
    values (lower(v_serial), '1234', 'worker', p_name, v_serial);
  end if;
end;
$$;

create or replace function public.ts_delete_worker(p_serial text)
returns void
language plpgsql security definer set search_path = public as $$
begin
  delete from public.workers  where serial = p_serial;
  delete from public.accounts where role = 'worker' and serial = p_serial;
end;
$$;

-- تحديث حدّ الحرارة (يستخدمه المختص)
create or replace function public.ts_set_threshold(p_val numeric)
returns void language sql security definer set search_path = public as $$
  update public.settings set temp_threshold = p_val where id = 1;
$$;

-- تحديث حرارة العامل (يستخدمه ESP + حساس DHT11) بالرقم التسلسلي
create or replace function public.ts_update_temp(p_serial text, p_temp numeric, p_hum numeric)
returns void language plpgsql security definer set search_path = public as $$
begin
  update public.workers set temperature = p_temp, humidity = p_hum, temp_at = now()
   where serial = p_serial;
  if not found then
    insert into public.workers(name, serial, temperature, humidity, temp_at, day, week, month, status)
    values ('عامل ' || p_serial, p_serial, p_temp, p_hum, now(), 'safe','safe','safe','safe');
  end if;
end;
$$;

-- منح صلاحية تنفيذ الدوال لمستخدم الواجهة (anon) والمستخدمين المسجّلين
grant execute on function public.ts_login(text,text)                         to anon, authenticated;
grant execute on function public.ts_signup(text,text,text,text,text)         to anon, authenticated;
grant execute on function public.ts_add_worker(text,text,text,boolean)       to anon, authenticated;
grant execute on function public.ts_delete_worker(text)                      to anon, authenticated;
grant execute on function public.ts_set_threshold(numeric)                   to anon, authenticated;
grant execute on function public.ts_update_temp(text,numeric,numeric)        to anon, authenticated;

-- ---------- المزامنة اللحظية (Realtime) ----------
-- مُحصّن ضد إعادة التشغيل: لا يُضيف الجدول إن كان موجوداً في النشر مسبقاً
do $$
begin
  if not exists (select 1 from pg_publication_tables
                 where pubname='supabase_realtime' and schemaname='public' and tablename='workers') then
    alter publication supabase_realtime add table public.workers;
  end if;
  if not exists (select 1 from pg_publication_tables
                 where pubname='supabase_realtime' and schemaname='public' and tablename='sos_alerts') then
    alter publication supabase_realtime add table public.sos_alerts;
  end if;
  if not exists (select 1 from pg_publication_tables
                 where pubname='supabase_realtime' and schemaname='public' and tablename='settings') then
    alter publication supabase_realtime add table public.settings;
  end if;
end $$;

-- ---------- بيانات ابتدائية (Seed) ----------
insert into public.settings(id, temp_threshold) values (1, 38)
on conflict (id) do nothing;

insert into public.accounts(username, password, role, name, serial) values
  ('worker',     '1234', 'worker',     'محمد أحمد العتيبي', 'TH-20481'),
  ('specialist', '1234', 'specialist', 'م. خالد الدوسري',   'SF-1001')
on conflict (username) do nothing;

insert into public.workers(name, serial, day, week, month, status) values
  ('محمد أحمد العتيبي', 'TH-20481', 'mid',    'high',   'danger', 'safe'),
  ('سالم ناصر القحطاني','TH-19872', 'safe',   'mid',    'high',   'mid'),
  ('أحمد علي الشهري',    'TH-17635', 'mid',    'mid',    'high',   'mid'),
  ('فهد عبدالله عسيري',  'TH-15092', 'high',   'high',   'danger', 'danger'),
  ('ناصر محمد آل دليم',  'TH-13457', 'danger', 'danger', 'danger', 'danger')
on conflict (serial) do nothing;

-- ============================================================
-- تم. قاعدة البيانات جاهزة. عُد إلى config.js وضع رابط المشروع ومفتاح anon.
-- ============================================================
