/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Ahmed Arif
 * Host-only visible-text locator; coordinates use the screenshot's top-left.
 */
#import <Foundation/Foundation.h>
#import <Vision/Vision.h>

int main(int argc, const char **argv)
{
    @autoreleasepool
    {
        if (argc != 2) return 2;
        VNRecognizeTextRequest *request = [[VNRecognizeTextRequest alloc] init];
        request.recognitionLevel = VNRequestTextRecognitionLevelAccurate;
        request.usesLanguageCorrection = NO;
        request.recognitionLanguages = @[@"en-US"];
        NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:argv[1]]];
        VNImageRequestHandler *handler = [[VNImageRequestHandler alloc] initWithURL:url options:@{}];
        NSError *error = nil;
        if (![handler performRequests:@[request] error:&error]) return 1;
        NSMutableArray *rows = [NSMutableArray array];
        for (VNRecognizedTextObservation *observation in request.results)
        {
            VNRecognizedText *candidate = [observation topCandidates:1].firstObject;
            if (!candidate) continue;
            CGRect box = observation.boundingBox;
            [rows addObject:@{@"text": candidate.string, @"x": @(CGRectGetMidX(box)), @"y": @(1 - CGRectGetMidY(box)), @"width": @(box.size.width), @"height": @(box.size.height)}];
        }
        NSData *json = [NSJSONSerialization dataWithJSONObject:rows options:NSJSONWritingSortedKeys error:&error];
        if (!json) return 1;
        [[NSFileHandle fileHandleWithStandardOutput] writeData:json];
        return 0;
    }
}
