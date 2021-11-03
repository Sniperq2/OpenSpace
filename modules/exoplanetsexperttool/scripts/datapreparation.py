# Based on implementation by Sebastian Zieba
# https://github.com/sebastian-zieba/TSM
#
# Downloads confirmed planet table from NExSci, aggregates data for ESM and TSM metrics, and saves 
# the resulting data in a csv file

import pandas as pd
import numpy as np
import os
import astropy
from datetime import datetime

NEW_API = 'https://exoplanetarchive.ipac.caltech.edu/TAP/sync?query='
# The "exoplanets" table includes all confirmed planets and hosts in the
# archive with parameters derived from a single, published reference

DATA_FOLDER = '../data/'
dataFileName = DATA_FOLDER + 'aggregated_data.csv'

# Create data folder if not exists
if not os.path.exists(DATA_FOLDER):
    os.makedirs(DATA_FOLDER)
    print('Created data folder')

###
## Download new confirmed planets
###
print("Downloading all confirmed planets from NExSci's Exoplanets Archive..")

columns='pl_name,hostname,default_flag,sy_snum,sy_pnum,discoverymethod,disc_year,disc_facility,tran_flag,soltype,pl_controv_flag,' \
        'pl_refname,pl_orbper,pl_orbpererr1,pl_orbpererr2,pl_orbperlim,pl_orbsmax,pl_orbsmaxerr1,pl_orbsmaxerr2,pl_orbsmaxlim,' \
        'pl_rade,pl_radeerr1,pl_radeerr2,pl_radelim,pl_radj,pl_radjerr1,pl_radjerr2,pl_radjlim,pl_bmasse,pl_bmasseerr1,pl_bmasseerr2,' \
        'pl_bmasselim,pl_bmassj,pl_bmassjerr1,pl_bmassjerr2,pl_bmassjlim,pl_bmassprov,pl_orbeccen,pl_orbeccenerr1,pl_orbeccenerr2,' \
        'pl_orbeccenlim,pl_insol,pl_insolerr1,pl_insolerr2,pl_insollim,pl_eqt,pl_eqterr1,pl_eqterr2,pl_eqtlim,' \
        'pl_orbincl,pl_orbinclerr1,pl_orbinclerr2,pl_orbincllim,ttv_flag,pl_trandur,pl_trandurerr1,pl_trandurerr2,pl_trandurlim,' \
        'pl_ratdor,pl_ratdorerr1,pl_ratdorerr2,pl_ratdorlim,pl_ratror,pl_ratrorerr1,pl_ratrorerr2,pl_ratrorlim,' \
        'pl_occdep,pl_occdeperr1,pl_occdeperr2,pl_occdeplim,' \
        'st_refname,st_spectype,st_teff,st_tefferr1,st_tefferr2,st_tefflim,st_rad,st_raderr1,st_raderr2,st_radlim,' \
        'st_mass,st_masserr1,st_masserr2,st_masslim,st_met,st_meterr1,st_meterr2,st_metlim,st_metratio,' \
        'st_logg,st_loggerr1,st_loggerr2,st_logglim,sy_refname,rastr,ra,decstr,dec,sy_dist,sy_disterr1,sy_disterr2,' \
        'sy_vmag,sy_vmagerr1,sy_vmagerr2,sy_jmag,sy_jmagerr1,sy_jmagerr2,sy_hmag,sy_hmagerr1,sy_hmagerr2,' \
        'sy_kmag,sy_kmagerr1,sy_kmagerr2,sy_gaiamag,sy_gaiamagerr1,sy_gaiamagerr2,rowupdate,pl_pubdate,releasedate' \


print("Downloading default_flag=1")
where1 = 'where+default_flag=1+and+tran_flag=1+and+upper%28soltype%29+like+%27%25CONF%25%27'
full1 = NEW_API + 'select+' + columns + '+from+ps+' + where1 + '&format=csv'
df0 = pd.read_csv(full1)

print("Downloading default_flag=0")
where0 = 'where+default_flag=0+and+tran_flag=1+and+upper%28soltype%29+like+%27%25CONF%25%27'
full0 = NEW_API + 'select+' + columns + '+from+ps+' + where0 + '&format=csv'
df1 = pd.read_csv(full0)

with open(DATA_FOLDER + 'last_update_time.txt', 'w+') as ff:
    ff.write(str(datetime.now()))

###
## Aggregate data
###
print("Aggregating data and computing additional parameters...")

# useful constants
from astropy import constants as const
Rjup = const.R_jup/const.R_earth
Mjup = const.M_jup/const.M_earth
ReRs = const.R_earth/const.R_sun
MeMs = const.M_earth/const.M_sun

# concatenates data sets
df = pd.concat([df0,df1])

# ## Convert the pubdate to a datetime object
# This is messy since there are three different datetime formats here.
# Must make sure to sort before filtering.

df['dt_obj'] = df['pl_pubdate']
df['dt_obj'] = pd.to_datetime(df['pl_pubdate'], format="%Y-%m", errors='ignore')
df['dt_obj'] = pd.to_datetime(df['pl_pubdate'], format="%Y-%m-%d", errors='ignore')
df['dt_obj'] = pd.to_datetime(df['pl_pubdate'], format="%Y-%m-%d %H:%M", errors='ignore')
df['dt_obj'] = pd.to_datetime(df['pl_pubdate'], format="%Y-%m", errors='raise')

df = df.sort_values(by='dt_obj', ascending=False)

## Build up a filter to kill off unwanted results
# Finding the Stassun paper
df.loc[df['pl_refname'].str.contains("STASSUN"), 'pl_refname'].unique()

data_filter = (
    (df['pl_refname'] != '<a refstr=STASSUN_ET_AL__2017 href=https://ui.adsabs.harvard.edu/abs/2017AJ....153..136S/abstract target=ref>Stassun et al. 2017</a>')
)

# ## Groupby planet name and grab the most recent record
cols = df.columns.to_list()[1:]
agg_dict = dict(zip(cols, ['first'] * len(cols)))

# aggregate
df = df[data_filter].groupby('pl_name', as_index = False).agg(agg_dict)
# df = df.groupby('pl_name', as_index = False).agg(agg_dict)

# fill in columns where mass or radius are only in Jupiter units
df.pl_rade.fillna(df.pl_radj*Rjup, inplace=True)
df.pl_bmasse.fillna(df.pl_bmassj*Mjup, inplace=True)

# ## Initialize new column for aggregate temperature.
# Try to use as few columns in the data as possible:
# first populate with insolation flux
df['pl_Teq'] = 278.*(df['pl_insol'])**0.25
# if insolation is unavailable, try equilibrium temperature
df.pl_Teq.fillna(df.pl_eqt, inplace=True)
# if equilibrium temperature is unavailable, calculate from a/Rs and Teff
df.pl_Teq.fillna(1/(np.sqrt(2*df['pl_ratdor']))*df['st_teff'], inplace=True)
# if a/Rs is unavailable, calculate it from a [AU] and Rs [Rsun]
df.pl_Teq.fillna(1/(np.sqrt(2*215.*df['pl_orbsmax']/df['st_rad']))*df['st_teff'], inplace=True)

df['pl_ratror'] = ReRs*df['pl_rade']/df['st_rad']
# initialize new column for (Rp/Rs)**2
df['pl_rprs2'] = df['pl_ratror']**2

# scale factor for TSM calculation https://arxiv.org/pdf/1805.03671.pdf
df['scale'] = 0
df.loc[df['pl_rade'] <= 1.5, 'scale'] = 0.19
df.loc[df['pl_rade'] > 4.0, 'scale'] = 1.15
df.loc[(df['pl_rade'] <= 4.0)&(df['pl_rade'] >2.75), 'scale'] = 1.28
df.loc[(df['pl_rade'] <= 2.75)&(df['pl_rade'] > 1.5), 'scale'] = 1.26

# Initialize new column for TSM
print("Computing TSM")
df['TSM'] = df['pl_rade'] * df['pl_rprs2']/(ReRs**2) * df['pl_Teq']/df['pl_bmasse'] * 10.**(-0.2*df['sy_jmag']) * df['scale']

# TODO: determine if efficiency and noise is relevant
# df['efficiency'] = 1
# df.loc[df['sy_jmag'] <= 8.8, 'efficiency'] = (1.375*df['sy_jmag']**2 - 1.214*df['sy_jmag'] - 26.68)/64.58
# df.loc[df['sy_jmag'] <= 5., 'efficiency'] = 0.3

# df['efficiency_kmag'] = 1
# df.loc[df['sy_kmag'] <= 8.8, 'efficiency_kmag'] = (1.375*df['sy_kmag']**2 - 1.214*df['sy_kmag'] - 26.68)/64.58
# df.loc[df['sy_kmag'] <= 5., 'efficiency_kmag'] = 0.3
# #correct TSM for observatinoal efficiency with HST/WFC3
# #df['TSM'] = df['TSM']*np.sqrt(df['efficiency'])

# noise_ref = 122 #photon noise wasp-103 122 ppm per 103 second exposure
# kmag_ref = 10.767
# df['noise'] = noise_ref * (10**(-0.4*(kmag_ref-df['sy_kmag'])))**(1/2)*(1/df['efficiency_kmag'])**(1/2)


# TODO: figure out if needed
# ## TODO: why are these here and not earlier? If computed earlier they could be used for computation
# # Fill ars if missing: a(AU)/Rs(Ro)*215
# df.pl_ratdor.fillna(df['pl_orbsmax']/df['st_rad']*215, inplace=True)

# # Fill insolation if missing: Ts^4/ars^2 * (215^2/5772^4) = Ts^4/ars^2 * 4.166e-11
# df.pl_insol.fillna(df['st_teff']**4/df['pl_ratdor']**2*4.166e-11, inplace=True)


h = const.h.value
c = const.c.value
kB = const.k_B.value

def Planck_ratio(Tstar, Tplanet_day, lam):
    res = (np.exp(h * c / (lam * kB * Tstar)) - 1)/ (np.exp(h * c / (lam * kB * Tplanet_day)) - 1)
    return res

print("Computing ESM")
df['ed_ESM'] = df['pl_rprs2'] * 1e6 * Planck_ratio(df['st_teff'], 1.1*df['pl_Teq'], 7.5e-6)
df['ESM'] = 4.29 * df['ed_ESM'] * 10**(-0.2*df['sy_kmag'])

# TODO: propagate errors, so we get uncertainties for ESM and TSM

print("Writing data to file...")
df.to_csv(dataFileName)
print("Done!")
