//
//  ViewController.m
//  libhooker-test
//
//  Created by CoolStar on 8/17/19.
//  Copyright © 2019 CoolStar. All rights reserved.
//

#import "ViewController.h"
#import "AppDelegate.h"

@interface ViewController ()

@end

@implementation ViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    
    AppDelegate2 *delegate = (AppDelegate2 *)UIApplication.sharedApplication.delegate;
    [delegate test];
    // Do any additional setup after loading the view.
}


@end
