-- ============================================================
-- ThermaSafe — مخطط قاعدة البيانات (النسخة 2)
-- يشمل: حرارة العامل تحدّد الحالة وتتراكم أسبوعي/شهري،
--       كل مختص له عماله، حرارة المدينة، تقارير لكل عامل.
-- شغّله في: Supabase → SQL Editor → New query → Run
-- ⚠️ يبدأ بحذف الجداول (آمن — يعيد إنشاء كل شيء + بيانات تجربة).
-- ============================================================

drop table if exists public.sos_alerts cascade;
drop table if exists public.accounts  cascade;
drop table if exists public.workers   cascade;
drop table if exists public.settings  cascade;

-- ---------- الجداول ----------
create table public.workers (
  id          bigint generated always as identity primary key,
  name        text not null,
  serial      text unique not null,
  owner       text,                         -- اسم مستخدم المختص المالك للعامل
  day         text not null default 'safe',
  week        text not null default 'safe',
  month       text not null default 'safe',
  status      text not null default 'safe',
  temperature numeric,
  humidity    numeric,
  temp_at     timestamptz,
  dose_week   numeric not null default 0,
  dose_month  numeric not null default 0,
  created_at  timestamptz not null default now()
);

create table public.accounts (
  id       bigint generated always as identity primary key,
  username text unique not null,
  password text not null,
  role     text not null check (role in ('worker','specialist')),
  name     text not null,
  serial   text
);

create table public.sos_alerts (
  id         bigint generated always as identity primary key,
  serial     text,
  name       text,
  location   text,
  created_at timestamptz not null default now()
);

create table public.settings (
  id             int primary key default 1,
  temp_threshold numeric not null default 38,
  city_temp      numeric not null default 40,
  constraint settings_single_row check (id = 1)
);

-- ---------- RLS ----------
alter table public.workers    enable row level security;
alter table public.accounts   enable row level security;
alter table public.sos_alerts enable row level security;
alter table public.settings   enable row level security;

drop policy if exists workers_read on public.workers;
create policy workers_read on public.workers for select using (true);
drop policy if exists settings_read on public.settings;
create policy settings_read on public.settings for select using (true);
drop policy if exists sos_read   on public.sos_alerts;
drop policy if exists sos_insert on public.sos_alerts;
create policy sos_read   on public.sos_alerts for select using (true);
create policy sos_insert on public.sos_alerts for insert with check (true);

-- ---------- دوال مساعدة ----------
-- مستوى الجرعة من قيمة تراكمية
create or replace function public.level_from_dose(d numeric)
returns text language sql immutable as $$
  select case when d < 15 then 'safe'
              when d < 45 then 'mid'
              when d < 90 then 'high'
              else 'danger' end;
$$;

-- الحالة اللحظية من الحرارة الفعّالة مقابل حدّ التحذير
create or replace function public.status_from_temp(eff numeric, thr numeric)
returns text language sql immutable as $$
  select case when eff >= thr       then 'danger'
              when eff >= thr - 2    then 'high'
              when eff >= thr - 6    then 'mid'
              else 'safe' end;
$$;

-- ---------- دوال الحساب/العمليات (RPC) ----------
create or replace function public.ts_login(p_user text, p_pass text)
returns table(username text, role text, name text, serial text)
language sql security definer set search_path = public as $$
  select username, role, name, serial from public.accounts
  where username = p_user and password = p_pass limit 1;
$$;

create or replace function public.ts_signup(
  p_user text, p_pass text, p_role text, p_name text, p_serial text
) returns table(username text, role text, name text, serial text)
language plpgsql security definer set search_path = public as $$
declare v_serial text; i int;
begin
  if exists (select 1 from public.accounts where username = p_user) then
    raise exception 'USER_EXISTS';
  end if;
  v_serial := coalesce(nullif(p_serial,''),
              (case when p_role='worker' then 'TH-' else 'SF-' end) || floor(random()*90000+10000)::int::text);
  insert into public.accounts(username,password,role,name,serial)
  values (p_user,p_pass,p_role,p_name,v_serial);
  if p_role='worker' and not exists (select 1 from public.workers where serial=v_serial) then
    insert into public.workers(name,serial,owner) values (p_name,v_serial,null);
  end if;
  -- كل مختص جديد يبدأ بثلاثة عمال تجريبيين تحت إدارته
  if p_role='specialist' then
    for i in 1..3 loop
      insert into public.workers(name,serial,owner)
      values ('عامل ' || i || ' - ' || p_name, 'TH-' || floor(random()*900000+100000)::int::text, p_user);
    end loop;
  end if;
  return query select p_user, p_role, p_name, v_serial;
end;
$$;

create or replace function public.ts_add_worker(
  p_name text, p_serial text, p_level text, p_account boolean, p_owner text
) returns void language plpgsql security definer set search_path = public as $$
declare v_serial text;
begin
  v_serial := coalesce(nullif(p_serial,''), 'TH-' || floor(random()*90000+10000)::int::text);
  insert into public.workers(name,serial,owner,day,week,month,status)
  values (p_name,v_serial,p_owner,p_level,p_level,p_level,p_level)
  on conflict (serial) do nothing;
  if p_account and not exists (select 1 from public.accounts where username = lower(v_serial)) then
    insert into public.accounts(username,password,role,name,serial)
    values (lower(v_serial),'1234','worker',p_name,v_serial);
  end if;
end;
$$;

create or replace function public.ts_delete_worker(p_serial text)
returns void language plpgsql security definer set search_path = public as $$
begin
  delete from public.workers  where serial = p_serial;
  delete from public.accounts where role='worker' and serial = p_serial;
end;
$$;

create or replace function public.ts_set_threshold(p_val numeric)
returns void language sql security definer set search_path = public as $$
  update public.settings set temp_threshold = p_val where id = 1;
$$;

create or replace function public.ts_set_city(p_val numeric)
returns void language sql security definer set search_path = public as $$
  update public.settings set city_temp = p_val where id = 1;
$$;

-- تحديث حرارة العامل: يضبط الحالة + يراكم الجرعة أسبوعي/شهري (يستخدمه ESP)
create or replace function public.ts_update_temp(p_serial text, p_temp numeric, p_hum numeric)
returns void language plpgsql security definer set search_path = public as $$
declare v_thr numeric; v_city numeric; v_eff numeric; v_status text; v_inc numeric;
begin
  select temp_threshold, city_temp into v_thr, v_city from public.settings where id = 1;
  v_thr  := coalesce(v_thr, 38);
  v_city := coalesce(v_city, 40);
  -- الحرارة الفعّالة = حرارة العامل + أثر حرارة المدينة المرتفعة
  v_eff := p_temp + greatest(0, v_city - 35) * 0.4;
  v_status := public.status_from_temp(v_eff, v_thr);
  v_inc := greatest(0, v_eff - (v_thr - 8));   -- تتراكم كلما اقترب/تجاوز الحد
  update public.workers
     set temperature = p_temp, humidity = p_hum, temp_at = now(),
         status = v_status,
         day    = v_status,
         dose_week  = dose_week  + v_inc,
         dose_month = dose_month + v_inc,
         week   = public.level_from_dose(dose_week  + v_inc),
         month  = public.level_from_dose(dose_month + v_inc)
   where serial = p_serial;
  if not found then
    insert into public.workers(name,serial,temperature,humidity,temp_at,status,day,week,month,dose_week,dose_month)
    values ('عامل '||p_serial, p_serial, p_temp, p_hum, now(), v_status, v_status,
            public.level_from_dose(v_inc), public.level_from_dose(v_inc), v_inc, v_inc);
  end if;
end;
$$;

grant execute on function public.ts_login(text,text)                       to anon, authenticated;
grant execute on function public.ts_signup(text,text,text,text,text)       to anon, authenticated;
grant execute on function public.ts_add_worker(text,text,text,boolean,text) to anon, authenticated;
grant execute on function public.ts_delete_worker(text)                    to anon, authenticated;
grant execute on function public.ts_set_threshold(numeric)                 to anon, authenticated;
grant execute on function public.ts_set_city(numeric)                      to anon, authenticated;
grant execute on function public.ts_update_temp(text,numeric,numeric)      to anon, authenticated;

-- ---------- المزامنة اللحظية ----------
do $$
begin
  if not exists (select 1 from pg_publication_tables where pubname='supabase_realtime' and schemaname='public' and tablename='workers') then
    alter publication supabase_realtime add table public.workers; end if;
  if not exists (select 1 from pg_publication_tables where pubname='supabase_realtime' and schemaname='public' and tablename='sos_alerts') then
    alter publication supabase_realtime add table public.sos_alerts; end if;
  if not exists (select 1 from pg_publication_tables where pubname='supabase_realtime' and schemaname='public' and tablename='settings') then
    alter publication supabase_realtime add table public.settings; end if;
end $$;

-- ---------- بيانات ابتدائية ----------
insert into public.settings(id, temp_threshold, city_temp) values (1, 38, 40)
on conflict (id) do nothing;

insert into public.accounts(username, password, role, name, serial) values
  ('worker',     '1234', 'worker',     'محمد أحمد العتيبي', 'TH-20481'),
  ('specialist', '1234', 'specialist', 'م. خالد الدوسري',   'SF-1001')
on conflict (username) do nothing;

-- ثلاثة عمال تحت إدارة المختص الافتراضي (specialist)، منهم حساب العامل التجريبي
insert into public.workers(name, serial, owner, day, week, month, status) values
  ('محمد أحمد العتيبي', 'TH-20481', 'specialist', 'safe', 'safe', 'safe', 'safe'),
  ('سالم ناصر القحطاني','TH-19872', 'specialist', 'safe', 'safe', 'safe', 'safe'),
  ('أحمد علي الشهري',    'TH-17635', 'specialist', 'safe', 'safe', 'safe', 'safe')
on conflict (serial) do nothing;

-- ============================================================
-- تم. تأكد أن config.js يحوي رابط المشروع والمفتاح.
-- ============================================================
